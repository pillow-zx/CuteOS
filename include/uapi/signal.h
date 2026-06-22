#ifndef _CUTEOS_UAPI_SIGNAL_H
#define _CUTEOS_UAPI_SIGNAL_H

/*
 * Signal ABI shared by kernel and user space.
 */

#define SIGHUP		1
#define SIGINT		2
#define SIGQUIT		3
#define SIGILL		4
#define SIGTRAP		5
#define SIGABRT		6
#define SIGBUS		7
#define SIGFPE		8
#define SIGKILL		9
#define SIGUSR1		10
#define SIGSEGV		11
#define SIGUSR2		12
#define SIGPIPE		13
#define SIGALRM		14
#define SIGTERM		15
#define SIGCHLD		17
#define SIGCONT		18
#define SIGSTOP		19
#define SIGSYS		31

#define NSIG		32

typedef void (*__sighandler_t)(int);
typedef void (*__sigrestorer_t)(void);

#define SIG_DFL		((__sighandler_t)0)
#define SIG_IGN		((__sighandler_t)1)
#define SIG_ERR		((__sighandler_t)-1)

#define SIG_BLOCK	0
#define SIG_UNBLOCK	1
#define SIG_SETMASK	2

struct sigaction {
	__sighandler_t sa_handler;
	unsigned long sa_flags;
	__sigrestorer_t sa_restorer;
	unsigned long sa_mask;
};

#define SIGNAL_EXIT_CODE(sig) (128 + (sig))

#endif
