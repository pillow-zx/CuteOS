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

/* sa_flags */
#define SA_ONSTACK	0x08000000
#define SA_RESTART	0x10000000
#define SA_NODEFER	0x40000000
#define SA_SIGINFO	0x00000004

/* alternate signal stack */
struct stack_t {
	void          *ss_sp;
	int            ss_flags;
	unsigned long  ss_size;
};

#define SS_ONSTACK	1
#define SS_DISABLE	2

#define MINSIGSTKSZ	2048
#define SIGSTKSZ	8192

#define SIGNAL_EXIT_CODE(sig) (128 + (sig))

#endif
