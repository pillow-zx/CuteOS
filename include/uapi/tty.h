#ifndef _CUTEOS_UAPI_TTY_H
#define _CUTEOS_UAPI_TTY_H

#define TCGETS	   0x5401
#define TCSETS	   0x5402
#define TCSETSW	   0x5403
#define TCSETSF	   0x5404
#define TIOCGWINSZ 0x5413
#define TIOCSWINSZ 0x5414

#define NCCS 19

#define VINTR	 0
#define VQUIT	 1
#define VERASE	 2
#define VKILL	 3
#define VEOF	 4
#define VTIME	 5
#define VMIN	 6
#define VSTART	 8
#define VSTOP	 9
#define VSUSP	 10
#define VEOL	 11
#define VREPRINT 12
#define VDISCARD 13
#define VWERASE	 14
#define VLNEXT	 15
#define VEOL2	 16

#define BRKINT 0000002
#define ICRNL  0000400
#define IXON   0002000

#define OPOST 0000001
#define ONLCR 0000004

#define B38400 0000017
#define CS8    0000060
#define CREAD  0000200

#define ISIG   0000001
#define ICANON 0000002
#define ECHO   0000010

typedef unsigned char cc_t;
typedef unsigned int speed_t;
typedef unsigned int tcflag_t;

struct termios {
	tcflag_t c_iflag;
	tcflag_t c_oflag;
	tcflag_t c_cflag;
	tcflag_t c_lflag;
	cc_t c_line;
	cc_t c_cc[NCCS];
};

struct winsize {
	unsigned short ws_row;
	unsigned short ws_col;
	unsigned short ws_xpixel;
	unsigned short ws_ypixel;
};

#endif
