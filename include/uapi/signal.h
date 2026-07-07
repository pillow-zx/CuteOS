#ifndef _CUTEOS_UAPI_SIGNAL_H
#define _CUTEOS_UAPI_SIGNAL_H

/**
 * @file signal.h
 * @brief Linux-compatible signal UAPI constants and layouts.
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

/**
 * @typedef __sighandler_t
 * @brief Userspace signal handler pointer taking the delivered signal number.
 */
typedef void (*__sighandler_t)(int);

/**
 * @typedef __sigrestorer_t
 * @brief Userspace trampoline pointer used to enter rt_sigreturn.
 */
typedef void (*__sigrestorer_t)(void);

/**
 * @union sigval
 * @brief POSIX timer/sigevent value passed back to userspace.
 *
 * @par Fields
 * - @c sival_int: Integer payload.
 * - @c sival_ptr: Pointer payload.
 */
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

/**
 * @struct sigaction
 * @brief Linux rt_sigaction layout used by userspace and the kernel.
 *
 * @par Fields
 * - @c sa_handler: Handler, SIG_DFL, or SIG_IGN.
 * - @c sa_flags: SA_* behavior flags.
 * - @c sa_restorer: Userspace restorer trampoline.
 * - @c sa_mask: Additional blocked signal mask.
 */
struct sigaction {
	__sighandler_t sa_handler;
	unsigned long sa_flags;
	__sigrestorer_t sa_restorer;
	unsigned long sa_mask;
};

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

/**
 * @struct sigevent
 * @brief Linux sigevent ABI layout for POSIX timer notification.
 *
 * The union padding keeps the structure at 64 bytes and preserves the Linux
 * offsets asserted below. cuteOS currently implements SIGEV_NONE and
 * SIGEV_SIGNAL semantics most deeply.
 *
 * @par Fields
 * - @c sigev_value: Payload delivered with timer notification.
 * - @c sigev_signo: Signal number for SIGEV_SIGNAL/THREAD_ID.
 * - @c sigev_notify: SIGEV_* notification mode.
 * - @c _pad: Reserved ABI padding.
 * - @c _tid: Target tid for SIGEV_THREAD_ID.
 * - @c _function: SIGEV_THREAD function.
 * - @c _attribute: SIGEV_THREAD attributes pointer.
 */
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

/**
 * @struct stack_t
 * @brief Linux sigaltstack userspace layout.
 *
 * @par Fields
 * - @c ss_sp: Base pointer of the alternate signal stack.
 * - @c ss_flags: SS_* state flags.
 * - @c ss_size: Stack size in bytes.
 */
struct stack_t {
	void *ss_sp;
	int ss_flags;
	unsigned long ss_size;
};

#define SS_ONSTACK 1
#define SS_DISABLE 2

#define MINSIGSTKSZ 2048
#define SIGSTKSZ    8192

/**
 * @def SIGNAL_EXIT_CODE
 * @brief Shell-visible exit status for a process killed by a signal.
 */
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
