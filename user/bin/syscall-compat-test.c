#include <ulib.h>

#define POLLIN	 0x0001
#define POLLOUT	 0x0004
#define POLLERR	 0x0008
#define POLLHUP	 0x0010
#define POLLNVAL 0x0020

#define GRND_NONBLOCK 0x0001
#define GRND_RANDOM   0x0002
#define GRND_INSECURE 0x0004

#define RLIMIT_NOFILE 7

#define RSEQ_FLAG_UNREGISTER 1

struct compat_pollfd {
	int fd;
	short events;
	short revents;
};

struct compat_statfs64 {
	long f_type;
	long f_bsize;
	unsigned long f_blocks;
	unsigned long f_bfree;
	unsigned long f_bavail;
	unsigned long f_files;
	unsigned long f_ffree;
	int f_fsid[2];
	long f_namelen;
	long f_frsize;
	long f_flags;
	long f_spare[4];
};

struct compat_rlimit64 {
	unsigned long rlim_cur;
	unsigned long rlim_max;
};

static int failures;

static void expect_eq_long(const char *name, long got, long want)
{
	if (got != want) {
		printf("syscall-compat-test: %s got %ld want %ld\n", name, got,
		       want);
		failures++;
	}
}

static void expect_true(const char *name, int ok)
{
	if (!ok) {
		printf("syscall-compat-test: %s failed\n", name);
		failures++;
	}
}

static int any_nonzero(const unsigned char *buf, size_t len)
{
	for (size_t i = 0; i < len; i++) {
		if (buf[i] != 0)
			return 1;
	}
	return 0;
}

static int timespec_ge(const struct timespec *a, const struct timespec *b)
{
	return a->tv_sec > b->tv_sec ||
	       (a->tv_sec == b->tv_sec && a->tv_nsec >= b->tv_nsec);
}

static void timespec_add_nsec(struct timespec *ts, long nsec)
{
	ts->tv_nsec += nsec;
	while (ts->tv_nsec >= 1000000000) {
		ts->tv_sec++;
		ts->tv_nsec -= 1000000000;
	}
}

static volatile unsigned long spin_sink;

static void test_nanosleep(void)
{
	struct timespec bad = {.tv_sec = 0, .tv_nsec = 1000000000};
	struct timespec ts = {.tv_sec = 0, .tv_nsec = 1000000};
	struct timespec before;
	struct timespec after;
	long ret;

	expect_eq_long("nanosleep invalid",
		       syscall(SYS_nanosleep, (long)&bad, 0), -EINVAL);

	expect_eq_long("clock_gettime before",
		       clock_gettime(CLOCK_MONOTONIC, &before), 0);
	ret = syscall(SYS_nanosleep, (long)&ts, 0);
	expect_eq_long("nanosleep short", ret, 0);
	expect_eq_long("clock_gettime after", clock_gettime(CLOCK_MONOTONIC,
							    &after),
		       0);
	expect_true("nanosleep elapsed",
		    after.tv_sec > before.tv_sec ||
			    (after.tv_sec == before.tv_sec &&
			     after.tv_nsec >= before.tv_nsec));
}

static void test_clock_nanosleep_relative(void)
{
	struct timespec ts = {.tv_sec = 0, .tv_nsec = 1000000};
	struct timespec before;
	struct timespec after;
	long ret;

	expect_eq_long("clock_nanosleep relative before",
		       clock_gettime(CLOCK_MONOTONIC, &before), 0);
	ret = clock_nanosleep(CLOCK_MONOTONIC, 0, &ts, 0);
	expect_eq_long("clock_nanosleep relative", ret, 0);
	expect_eq_long("clock_nanosleep relative after",
		       clock_gettime(CLOCK_MONOTONIC, &after), 0);
	expect_true("clock_nanosleep relative elapsed",
		    timespec_ge(&after, &before));
}

static void test_clock_nanosleep_absolute_and_errors(void)
{
	struct timespec deadline;
	struct timespec after;
	struct timespec bad = {.tv_sec = 0, .tv_nsec = 1000000000};
	struct timespec short_ts = {.tv_sec = 0, .tv_nsec = 1000000};

	expect_eq_long("clock_nanosleep absolute before",
		       clock_gettime(CLOCK_MONOTONIC, &deadline), 0);
	timespec_add_nsec(&deadline, 1000000);
	expect_eq_long("clock_nanosleep absolute",
		       clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME,
				       &deadline, 0),
		       0);
	expect_eq_long("clock_nanosleep absolute after",
		       clock_gettime(CLOCK_MONOTONIC, &after), 0);
	expect_true("clock_nanosleep absolute elapsed",
		    timespec_ge(&after, &deadline));

	expect_eq_long("clock_nanosleep bad flags",
		       clock_nanosleep(CLOCK_MONOTONIC, 0x8000, &short_ts, 0),
		       -EINVAL);
	expect_eq_long("clock_nanosleep bad nsec",
		       clock_nanosleep(CLOCK_MONOTONIC, 0, &bad, 0),
		       -EINVAL);
	expect_eq_long("clock_nanosleep null req",
		       clock_nanosleep(CLOCK_MONOTONIC, 0, 0, 0), -EFAULT);
	expect_eq_long("clock_nanosleep bad clock",
		       clock_nanosleep(99, 0, &short_ts, 0), -EINVAL);
}

static void test_sched_getaffinity(void)
{
	unsigned long mask = ~0UL;

	expect_eq_long("sched_getaffinity current",
		       sched_getaffinity(0, sizeof(mask), &mask),
		       (long)sizeof(mask));
	expect_eq_long("sched_getaffinity cpu0 mask", (long)mask, 1);
}

static void test_sched_setaffinity(void)
{
	unsigned long cpu0 = 1;
	unsigned long empty = 0;
	unsigned long cpu1 = 2;

	expect_eq_long("sched_setaffinity cpu0",
		       sched_setaffinity(0, sizeof(cpu0), &cpu0), 0);
	expect_eq_long("sched_setaffinity empty",
		       sched_setaffinity(0, sizeof(empty), &empty), -EINVAL);
	expect_eq_long("sched_setaffinity cpu1 only",
		       sched_setaffinity(0, sizeof(cpu1), &cpu1), -EINVAL);
	expect_eq_long("sched_setaffinity tid",
		       sched_setaffinity(gettid(), sizeof(cpu0), &cpu0), 0);
	expect_eq_long("sched_setaffinity missing tid",
		       sched_setaffinity(9999, sizeof(cpu0), &cpu0), -ESRCH);
}

static void test_times_accounting(void)
{
	struct tms before;
	struct tms after;
	long start;
	long now = 0;

	start = times(&before);
	expect_true("times start", start >= 0);

	for (int attempt = 0; attempt < 1000; attempt++) {
		for (int i = 0; i < 20000; i++)
			spin_sink = spin_sink * 1664525UL + 1013904223UL;

		now = times(&after);
		if (now > start)
			break;
	}

	expect_true("times elapsed tick", now > start);
	expect_true("times cpu accounted",
		    after.tms_utime + after.tms_stime >
			    before.tms_utime + before.tms_stime);
}

static void test_sysinfo_fields(void)
{
	struct sysinfo info;

	memset(&info, 0, sizeof(info));
	expect_eq_long("sysinfo", sysinfo(&info), 0);
	expect_eq_long("sysinfo mem_unit", info.mem_unit, 1);
	expect_true("sysinfo totalram", info.totalram > 0);
	expect_true("sysinfo freeram",
		    info.freeram > 0 && info.freeram <= info.totalram);
	expect_true("sysinfo procs", info.procs >= 1);
	expect_true("sysinfo uptime", info.uptime >= 0);
}

static void test_getrandom(void)
{
	unsigned char buf[32];
	unsigned char zero[32];

	memset(buf, 0, sizeof(buf));
	memset(zero, 0, sizeof(zero));
	expect_eq_long("getrandom length",
		       syscall(SYS_getrandom, (long)buf, sizeof(buf),
			       GRND_NONBLOCK | GRND_INSECURE),
		       (long)sizeof(buf));
	(void)zero;
	expect_true("getrandom nonzero", any_nonzero(buf, sizeof(buf)));
	expect_eq_long("getrandom bad flags",
		       syscall(SYS_getrandom, (long)buf, sizeof(buf), 0x8000),
		       -EINVAL);
}

static void test_prlimit64(void)
{
	struct compat_rlimit64 lim;
	struct compat_rlimit64 old;
	struct compat_rlimit64 set = {.rlim_cur = 16, .rlim_max = 16};
	int status = -1;
	long pid;

	expect_eq_long("prlimit get",
		       syscall(SYS_prlimit64, 0, RLIMIT_NOFILE, 0, (long)&lim),
		       0);
	expect_true("prlimit nofile sane", lim.rlim_cur >= 3);
	expect_eq_long("prlimit set",
		       syscall(SYS_prlimit64, 0, RLIMIT_NOFILE, (long)&set,
			       (long)&old),
		       0);
	expect_eq_long("prlimit get changed",
		       syscall(SYS_prlimit64, 0, RLIMIT_NOFILE, 0, (long)&lim),
		       0);
	expect_eq_long("prlimit nofile cur", lim.rlim_cur, 16);
	expect_eq_long("prlimit bad resource",
		       syscall(SYS_prlimit64, 0, 999, 0, (long)&lim), -EINVAL);

	pid = fork();
	if (pid == 0) {
		struct compat_rlimit64 child_lim;

		if (syscall(SYS_prlimit64, 0, RLIMIT_NOFILE, 0,
			    (long)&child_lim) == 0 &&
		    child_lim.rlim_cur == 16 && child_lim.rlim_max == 16)
			exit(0);
		exit(1);
	}
	expect_true("prlimit fork child created", pid > 0);
	if (pid > 0) {
		expect_eq_long("prlimit fork wait", wait4(pid, &status, 0, 0),
			       pid);
		expect_eq_long("prlimit fork inherit", status, 0);
	}
}

static void test_statfs64(void)
{
	struct compat_statfs64 st;
	int fd;

	expect_eq_long("statfs64 root",
		       syscall(SYS_statfs64, (long)"/", (long)&st), 0);
	expect_true("statfs64 blocks", st.f_bsize > 0 && st.f_blocks > 0);
	expect_true("statfs64 namelen", st.f_namelen >= 255);

	fd = open("/", O_RDONLY | O_DIRECTORY);
	expect_true("open root dir", fd >= 0);
	if (fd >= 0) {
		expect_eq_long("fstatfs64 root",
			       syscall(SYS_fstatfs64, fd, (long)&st),
			       0);
		close(fd);
	}
	expect_eq_long("fstatfs64 bad fd",
		       syscall(SYS_fstatfs64, 1234, (long)&st), -EBADF);
}

static void test_ppoll(void)
{
	int fds[2];
	char ch = 'x';
	struct timespec zero = {0, 0};
	struct compat_pollfd pfd;

	expect_eq_long("pipe", pipe(fds), 0);

	memset(&pfd, 0, sizeof(pfd));
	pfd.fd = fds[0];
	pfd.events = POLLIN;
	expect_eq_long("ppoll empty pipe",
		       syscall(SYS_ppoll, (long)&pfd, 1, (long)&zero, 0,
			       sizeof(unsigned long)),
		       0);
	expect_eq_long("write pipe byte", write(fds[1], &ch, 1), 1);
	expect_eq_long("ppoll pipe readable",
		       syscall(SYS_ppoll, (long)&pfd, 1, (long)&zero, 0,
			       sizeof(unsigned long)),
		       1);
	expect_true("ppoll revents pollin", (pfd.revents & POLLIN) != 0);

	pfd.fd = -1;
	pfd.events = POLLIN;
	pfd.revents = 0;
	expect_eq_long("ppoll ignored negative fd",
		       syscall(SYS_ppoll, (long)&pfd, 1, (long)&zero, 0,
			       sizeof(unsigned long)),
		       0);
	expect_eq_long("ppoll negative revents", pfd.revents, 0);

	pfd.fd = 1234;
	pfd.events = POLLIN;
	pfd.revents = 0;
	expect_eq_long("ppoll bad fd",
		       syscall(SYS_ppoll, (long)&pfd, 1, (long)&zero, 0,
			       sizeof(unsigned long)),
		       1);
	expect_true("ppoll pollnval", (pfd.revents & POLLNVAL) != 0);

	close(fds[0]);
	close(fds[1]);
}

static void test_rseq(void)
{
	expect_eq_long("rseq fallback",
		       syscall(SYS_rseq, 0, 0, RSEQ_FLAG_UNREGISTER, 0),
		       -ENOSYS);
}

static void test_ioctl(void)
{
	struct termios tio;
	struct termios original;
	struct termios updated;
	struct winsize ws;
	struct winsize original_ws;
	int fd;

	expect_eq_long("ioctl bad fd",
		       ioctl(1234, TCGETS, 0), -EBADF);

	fd = open("/", O_RDONLY | O_DIRECTORY);
	expect_true("ioctl open root dir", fd >= 0);
	if (fd >= 0) {
		expect_eq_long("ioctl directory tcgets",
			       ioctl(fd, TCGETS, 0), -ENOTTY);
		close(fd);
	}

	memset(&tio, 0, sizeof(tio));
	expect_eq_long("ioctl stdio tcgets", ioctl(0, TCGETS, (long)&tio), 0);
	original = tio;
	expect_true("ioctl tcgets has speed flags",
		    (tio.c_cflag & (B38400 | CS8 | CREAD)) ==
			    (B38400 | CS8 | CREAD));
	expect_true("ioctl tcgets has local flags",
		    (tio.c_lflag & (ICANON | ECHO | ISIG)) ==
			    (ICANON | ECHO | ISIG));
	expect_eq_long("ioctl tcgets has vsusp disabled", tio.c_cc[VSUSP], 0);
	expect_eq_long("ioctl tcgets null", ioctl(0, TCGETS, 0), -EFAULT);

	updated = tio;
	updated.c_lflag &= ~ECHO;
	expect_eq_long("ioctl stdio tcsets", ioctl(0, TCSETS, (long)&updated),
		       0);
	memset(&tio, 0, sizeof(tio));
	expect_eq_long("ioctl tcgets after tcsets",
		       ioctl(0, TCGETS, (long)&tio), 0);
	expect_true("ioctl tcsets persists echo off", (tio.c_lflag & ECHO) == 0);
	expect_eq_long("ioctl tcsetsw", ioctl(0, TCSETSW, (long)&tio), 0);
	expect_eq_long("ioctl tcsetsf", ioctl(0, TCSETSF, (long)&tio), 0);
	expect_eq_long("ioctl tcsets null", ioctl(0, TCSETS, 0), -EFAULT);
	expect_eq_long("ioctl restore termios",
		       ioctl(0, TCSETS, (long)&original), 0);

	memset(&ws, 0, sizeof(ws));
	expect_eq_long("ioctl tiocgwinsz", ioctl(0, TIOCGWINSZ, (long)&ws), 0);
	original_ws = ws;
	expect_true("ioctl winsize default", ws.ws_row > 0 && ws.ws_col > 0);
	ws.ws_row = 40;
	ws.ws_col = 100;
	ws.ws_xpixel = 0;
	ws.ws_ypixel = 0;
	expect_eq_long("ioctl tiocswinsz", ioctl(0, TIOCSWINSZ, (long)&ws), 0);
	memset(&ws, 0, sizeof(ws));
	expect_eq_long("ioctl tiocgwinsz after set",
		       ioctl(0, TIOCGWINSZ, (long)&ws), 0);
	expect_eq_long("ioctl winsize row", ws.ws_row, 40);
	expect_eq_long("ioctl winsize col", ws.ws_col, 100);
	expect_eq_long("ioctl tiocgwinsz null", ioctl(0, TIOCGWINSZ, 0),
		       -EFAULT);
	expect_eq_long("ioctl restore winsize",
		       ioctl(0, TIOCSWINSZ, (long)&original_ws), 0);
	expect_eq_long("ioctl stdio unknown", ioctl(0, 0xdeadbeef, 0),
		       -ENOTTY);
}

int main(void)
{
	test_nanosleep();
	test_clock_nanosleep_relative();
	test_clock_nanosleep_absolute_and_errors();
	test_sched_getaffinity();
	test_sched_setaffinity();
	test_times_accounting();
	test_sysinfo_fields();
	test_getrandom();
	test_prlimit64();
	test_statfs64();
	test_ppoll();
	test_rseq();
	test_ioctl();

	if (failures) {
		printf("syscall-compat-test: %d failures\n", failures);
		return 1;
	}

	printf("syscall-compat-test: PASS\n");
	return 0;
}
