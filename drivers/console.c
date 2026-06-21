/*
 * drivers/console.c - UART-backed console file operations
 */

#include <drivers/console.h>
#include <drivers/uart.h>
#include <kernel/errno.h>
#include <kernel/mm.h>
#include <kernel/signal.h>
#include <kernel/string.h>
#include <kernel/task.h>
#include <uapi/tty.h>

#define CONSOLE_INPUT_SIZE 256

#define TTY_INPUT_CONTINUE 0
#define TTY_INPUT_READY    1
#define TTY_INPUT_EOF	    2
#define TTY_INPUT_SIGNAL   3

typedef void (*console_emit_fn)(char ch, void *ctx);

static ssize_t console_read(struct file *file, char *buf, size_t count);
static ssize_t console_write(struct file *file, const char *buf, size_t count);
static uint32_t console_poll(struct file *file, uint32_t events);
static int console_ioctl(struct file *file, uint64_t cmd, uint64_t arg);

static struct termios console_termios = {
	.c_iflag = BRKINT | ICRNL | IXON,
	.c_oflag = OPOST | ONLCR,
	.c_cflag = B38400 | CS8 | CREAD,
	.c_lflag = ISIG | ICANON | ECHO,
	.c_cc = {
		[VINTR] = 3,	  /* Ctrl-C */
		[VQUIT] = 28,	  /* Ctrl-\ */
		[VERASE] = 127,	  /* DEL */
		[VKILL] = 21,	  /* Ctrl-U */
		[VEOF] = 4,	  /* Ctrl-D */
		[VTIME] = 0,
		[VMIN] = 1,
		[VSTART] = 17,	  /* Ctrl-Q */
		[VSTOP] = 19,	  /* Ctrl-S */
		[VSUSP] = 0,
		[VEOL] = 0,
		[VREPRINT] = 18,  /* Ctrl-R */
		[VDISCARD] = 15,  /* Ctrl-O */
		[VWERASE] = 23,	  /* Ctrl-W */
		[VLNEXT] = 22,	  /* Ctrl-V */
		[VEOL2] = 0,
	},
};

static struct winsize console_winsize = {
	.ws_row = 24,
	.ws_col = 80,
};

static char console_input[CONSOLE_INPUT_SIZE];
static size_t console_input_len;
static size_t console_input_pos;
static bool console_input_eof;

const struct file_operations console_fops = {
	.read = console_read,
	.write = console_write,
	.poll = console_poll,
	.ioctl = console_ioctl,
};

static void console_uart_emit(char ch, void *ctx)
{
	(void)ctx;
	uart_putc(ch);
}

static void console_emit_output(const struct termios *termios, char ch,
				console_emit_fn emit, void *ctx)
{
	if ((termios->c_oflag & OPOST) && (termios->c_oflag & ONLCR) &&
	    ch == '\n')
		emit('\r', ctx);
	emit(ch, ctx);
}

static size_t console_write_translated(const struct termios *termios,
				       const char *buf, size_t count,
				       console_emit_fn emit, void *ctx)
{
	size_t emitted = 0;

	for (size_t i = 0; i < count; i++) {
		if ((termios->c_oflag & OPOST) &&
		    (termios->c_oflag & ONLCR) && buf[i] == '\n') {
			emit('\r', ctx);
			emitted++;
		}
		emit(buf[i], ctx);
		emitted++;
	}

	return emitted;
}

static void console_echo_char(const struct termios *termios, char ch,
			      console_emit_fn emit, void *ctx)
{
	if (!(termios->c_lflag & ECHO))
		return;
	console_emit_output(termios, ch, emit, ctx);
}

static void console_echo_erase(console_emit_fn emit, void *ctx)
{
	emit('\b', ctx);
	emit(' ', ctx);
	emit('\b', ctx);
}

static bool console_cc_eq(const struct termios *termios, int idx, char ch)
{
	return termios->c_cc[idx] != 0 && ch == (char)termios->c_cc[idx];
}

static int console_signal_for_char(const struct termios *termios, char ch)
{
	if (!(termios->c_lflag & ISIG))
		return 0;
	if (console_cc_eq(termios, VINTR, ch))
		return SIGINT;
	if (console_cc_eq(termios, VQUIT, ch))
		return SIGQUIT;
	return 0;
}

static char console_translate_input(const struct termios *termios, char ch)
{
	if ((termios->c_iflag & ICRNL) && ch == '\r')
		return '\n';
	return ch;
}

static int console_canonical_accept(const struct termios *termios, char ch,
				    char *line, size_t *line_len,
				    size_t line_cap, console_emit_fn emit,
				    void *ctx, int *signal)
{
	int sig = console_signal_for_char(termios, ch);

	if (sig) {
		*line_len = 0;
		*signal = sig;
		return TTY_INPUT_SIGNAL;
	}

	ch = console_translate_input(termios, ch);

	if (console_cc_eq(termios, VEOF, ch))
		return *line_len == 0 ? TTY_INPUT_EOF : TTY_INPUT_READY;

	if (console_cc_eq(termios, VERASE, ch) || ch == '\b' ||
	    ch == 0x7f) {
		if (*line_len > 0) {
			(*line_len)--;
			if (termios->c_lflag & ECHO)
				console_echo_erase(emit, ctx);
		}
		return TTY_INPUT_CONTINUE;
	}

	if (console_cc_eq(termios, VKILL, ch)) {
		while (*line_len > 0) {
			(*line_len)--;
			if (termios->c_lflag & ECHO)
				console_echo_erase(emit, ctx);
		}
		return TTY_INPUT_CONTINUE;
	}

	if (*line_len + 1 < line_cap) {
		line[*line_len] = ch;
		(*line_len)++;
		console_echo_char(termios, ch, emit, ctx);
	}

	if (ch == '\n' || console_cc_eq(termios, VEOL, ch) ||
	    console_cc_eq(termios, VEOL2, ch))
		return TTY_INPUT_READY;
	return TTY_INPUT_CONTINUE;
}

static int console_raw_accept(const struct termios *termios, char raw,
			      char *out, size_t *out_len, size_t out_cap,
			      console_emit_fn emit, void *ctx, int *signal)
{
	int sig = console_signal_for_char(termios, raw);
	char ch;

	if (sig) {
		*signal = sig;
		return TTY_INPUT_SIGNAL;
	}

	ch = console_translate_input(termios, raw);
	if (*out_len < out_cap) {
		out[*out_len] = ch;
		(*out_len)++;
		console_echo_char(termios, ch, emit, ctx);
	}

	return TTY_INPUT_CONTINUE;
}

static ssize_t console_copy_pending(char *buf, size_t count)
{
	size_t available = console_input_len - console_input_pos;
	size_t n = available < count ? available : count;

	memcpy(buf, console_input + console_input_pos, n);
	console_input_pos += n;
	if (console_input_pos == console_input_len) {
		console_input_pos = 0;
		console_input_len = 0;
	}

	return (ssize_t)n;
}

static ssize_t console_read(struct file *file, char *buf, size_t count)
{
	(void)file;

	if (count == 0)
		return 0;

	if (!(console_termios.c_lflag & ICANON)) {
		size_t done = 0;
		size_t vmin = console_termios.c_cc[VMIN];

		if (vmin == 0)
			vmin = 1;
		if (vmin > count)
			vmin = count;
		while (done < vmin) {
			char ch = (char)uart_getc();
			int signal = 0;
			int event = console_raw_accept(
				&console_termios, ch, buf, &done, count,
				console_uart_emit, NULL, &signal);

			if (event == TTY_INPUT_SIGNAL) {
				if (signal)
					send_signal(signal, current);
				return done ? (ssize_t)done : -EINTR;
			}
		}
		return (ssize_t)done;
	}

	while (1) {
		if (console_input_pos < console_input_len)
			return console_copy_pending(buf, count);
		if (console_input_eof) {
			console_input_eof = false;
			return 0;
		}

		while (1) {
			char ch = (char)uart_getc();
			int signal = 0;
			int event = console_canonical_accept(
				&console_termios, ch, console_input,
				&console_input_len, sizeof(console_input),
				console_uart_emit, NULL, &signal);

			if (event == TTY_INPUT_CONTINUE)
				continue;
			if (event == TTY_INPUT_SIGNAL) {
				if (signal)
					send_signal(signal, current);
				return -EINTR;
			}
			if (event == TTY_INPUT_EOF)
				console_input_eof = true;
			break;
		}
	}
}

static ssize_t console_write(struct file *file, const char *buf, size_t count)
{
	(void)file;

	console_write_translated(&console_termios, buf, count, console_uart_emit,
				 NULL);

	return (ssize_t)count;
}

#ifdef CONFIG_KERNEL_TEST
struct console_emit_buffer {
	char *data;
	size_t len;
	size_t cap;
};

static void console_buffer_emit(char ch, void *ctx)
{
	struct console_emit_buffer *buf = ctx;

	if (buf->len < buf->cap)
		buf->data[buf->len] = ch;
	buf->len++;
}

ssize_t console_tty_write_for_test(const struct termios *termios,
				   const char *input, size_t input_len,
				   char *out, size_t out_size)
{
	struct console_emit_buffer emit = {
		.data = out,
		.cap = out_size,
	};

	console_write_translated(termios, input, input_len, console_buffer_emit,
				 &emit);
	return (ssize_t)emit.len;
}

ssize_t console_tty_read_stream_for_test(const struct termios *termios,
					 const char *input, size_t input_len,
					 char *out, size_t out_size,
					 char *echo, size_t echo_size,
					 int *signal)
{
	struct console_emit_buffer echo_buf = {
		.data = echo,
		.cap = echo_size,
	};
	size_t out_len = 0;

	*signal = 0;
	for (size_t i = 0; i < input_len; i++) {
		int event;

		if (termios->c_lflag & ICANON)
			event = console_canonical_accept(
				termios, input[i], out, &out_len, out_size,
				console_buffer_emit, &echo_buf, signal);
		else
			event = console_raw_accept(termios, input[i], out,
						   &out_len, out_size,
						   console_buffer_emit,
						   &echo_buf, signal);

		if (event == TTY_INPUT_SIGNAL)
			return -EINTR;
		if (event == TTY_INPUT_EOF || event == TTY_INPUT_READY)
			return (ssize_t)out_len;
	}

	return (ssize_t)out_len;
}
#endif

static uint32_t console_poll(struct file *file, uint32_t events)
{
	uint32_t mask = 0;

	if ((events & POLLIN) && (file->f_mode & FMODE_READ))
		mask |= POLLIN;
	if ((events & POLLOUT) && (file->f_mode & FMODE_WRITE))
		mask |= POLLOUT;
	return mask;
}

static int console_ioctl(struct file *file, uint64_t cmd, uint64_t arg)
{
	struct termios termios;
	struct winsize winsize;

	(void)file;

	switch (cmd) {
	case TCGETS:
		if (copy_to_user((void *)arg, &console_termios,
				 sizeof(console_termios)) != 0)
			return -EFAULT;
		return 0;
	case TCSETS:
	case TCSETSW:
	case TCSETSF:
		if (copy_from_user(&termios, (const void *)arg,
				   sizeof(termios)) != 0)
			return -EFAULT;
		console_termios = termios;
		return 0;
	case TIOCGWINSZ:
		if (copy_to_user((void *)arg, &console_winsize,
				 sizeof(console_winsize)) != 0)
			return -EFAULT;
		return 0;
	case TIOCSWINSZ:
		if (copy_from_user(&winsize, (const void *)arg,
				   sizeof(winsize)) != 0)
			return -EFAULT;
		console_winsize = winsize;
		return 0;
	}

	return -ENOTTY;
}
