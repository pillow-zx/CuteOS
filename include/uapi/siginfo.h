#ifndef _CUTEOS_UAPI_SIGINFO_H
#define _CUTEOS_UAPI_SIGINFO_H

#define SI_MAX_SIZE 128

#define SI_USER	  0
#define SI_KERNEL 0x80
#define SI_QUEUE  -1
#define SI_TIMER  -2

#define ILL_ILLOPC 1
#define ILL_ILLTRP 4

#define SEGV_MAPERR 1
#define SEGV_ACCERR 2

#define BUS_ADRALN 1

#define TRAP_BRKPT 1

#define CLD_EXITED 1

union siginfo_fields {
	struct {
		int pid;
		unsigned int uid;
	} kill;
	struct {
		int tid;
		int overrun;
		union {
			int sival_int;
			void *sival_ptr;
		} sigval;
		int sys_private;
	} timer;
	struct {
		int pid;
		unsigned int uid;
		int status;
		long utime;
		long stime;
	} sigchld;
	struct {
		void *addr;
		int trapno;
	} sigfault;
};

typedef struct siginfo {
	union {
		struct {
			int si_signo;
			int si_errno;
			int si_code;
			int __pad0;
			union siginfo_fields fields;
		};
		int __pad[SI_MAX_SIZE / sizeof(int)];
	};
} siginfo_t;

#define si_pid	   fields.kill.pid
#define si_uid	   fields.kill.uid
#define si_tid	   fields.timer.tid
#define si_overrun fields.timer.overrun
#define si_value   fields.timer.sigval
#define si_int	   fields.timer.sigval.sival_int
#define si_ptr	   fields.timer.sigval.sival_ptr
#define si_status  fields.sigchld.status
#define si_utime   fields.sigchld.utime
#define si_stime   fields.sigchld.stime
#define si_addr	   fields.sigfault.addr
#define si_trapno  fields.sigfault.trapno

_Static_assert(sizeof(siginfo_t) == SI_MAX_SIZE,
	       "Linux siginfo_t size mismatch");

#endif
