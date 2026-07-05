#ifndef _CUTEOS_UAPI_SIGNAL_H
#define _CUTEOS_UAPI_SIGNAL_H

/*
 * Signal ABI shared by kernel and user space.
 */

#define SIGHUP	1
#define SIGINT	2
#define SIGQUIT 3
#define SIGILL	4
#define SIGTRAP 5
#define SIGABRT 6
#define SIGBUS	7
#define SIGFPE	8
#define SIGKILL 9
#define SIGUSR1 10
#define SIGSEGV 11
#define SIGUSR2 12
#define SIGPIPE 13
#define SIGALRM 14
#define SIGTERM 15
#define SIGCHLD 17
#define SIGCONT 18
#define SIGSTOP 19
#define SIGSYS	31

#define NSIG 32

typedef void (*__sighandler_t)(int);
typedef void (*__sigrestorer_t)(void);

typedef union sigval {
	int sival_int;
	void *sival_ptr;
} sigval_t;

#define SIG_DFL ((__sighandler_t)0)
#define SIG_IGN ((__sighandler_t)1)
#define SIG_ERR ((__sighandler_t) - 1)

#define SIG_BLOCK   0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2

struct sigaction {
	__sighandler_t sa_handler;
	unsigned long sa_flags;
	__sigrestorer_t sa_restorer;
	unsigned long sa_mask;
};

/* sa_flags */
#define SA_ONSTACK 0x08000000
#define SA_RESTART 0x10000000
#define SA_NODEFER 0x40000000
#define SA_SIGINFO 0x00000004

#define SIGEV_SIGNAL	0
#define SIGEV_NONE	1
#define SIGEV_THREAD	2
#define SIGEV_THREAD_ID 4

#define SIGEV_MAX_SIZE	    64
#define SIGEV_PREAMBLE_SIZE (sizeof(int) * 2 + sizeof(sigval_t))
#define SIGEV_PAD_SIZE ((SIGEV_MAX_SIZE - SIGEV_PREAMBLE_SIZE) / sizeof(int))

typedef struct sigevent {
	sigval_t sigev_value;
	int sigev_signo;
	int sigev_notify;
	union {
		int _pad[SIGEV_PAD_SIZE];
		int _tid;
		struct {
			void (*_function)(sigval_t);
			void *_attribute;
		} _sigev_thread;
	} _sigev_un;
} sigevent_t;

#define sigev_notify_function	_sigev_un._sigev_thread._function
#define sigev_notify_attributes _sigev_un._sigev_thread._attribute
#define sigev_notify_thread_id	_sigev_un._tid

/* alternate signal stack */
struct stack_t {
	void *ss_sp;
	int ss_flags;
	unsigned long ss_size;
};

#define SS_ONSTACK 1
#define SS_DISABLE 2

#define MINSIGSTKSZ 2048
#define SIGSTKSZ    8192

#define SIGNAL_EXIT_CODE(sig) (128 + (sig))

#undef offsetof
#define offsetof(t, d) __builtin_offsetof(t, d)

_Static_assert(sizeof(sigval_t) == 8, "sigval_t ABI size mismatch");
_Static_assert(sizeof(sigevent_t) == 64, "sigevent_t ABI size mismatch");
_Static_assert(offsetof(sigevent_t, sigev_signo) == 8,
	       "sigevent signo ABI offset mismatch");
_Static_assert(offsetof(sigevent_t, sigev_notify) == 12,
	       "sigevent notify ABI offset mismatch");
_Static_assert(offsetof(sigevent_t, sigev_notify_thread_id) == 16,
	       "sigevent thread id ABI offset mismatch");
_Static_assert(SIGEV_SIGNAL == 0, "SIGEV_SIGNAL ABI value mismatch");
_Static_assert(SIGEV_NONE == 1, "SIGEV_NONE ABI value mismatch");
_Static_assert(SIGEV_THREAD == 2, "SIGEV_THREAD ABI value mismatch");
_Static_assert(SIGEV_THREAD_ID == 4, "SIGEV_THREAD_ID ABI value mismatch");

#endif
