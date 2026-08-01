#ifndef _CUTEOS_KERNEL_SESSION_H
#define _CUTEOS_KERNEL_SESSION_H

/*
 * include/kernel/session.h - process session and controlling-TTY policy
 */

#include <kernel/compiler.h>
#include <kernel/types.h>

struct task_struct;

__must_check
int session_process_clone_prepare(struct task_struct *child, struct task_struct *parent,
				  bool share_thread_group) ;

__must_check
int session_process_setsid(struct task_struct *task) ;

__must_check
int session_process_setpgid(pid_t pid, pid_t pgid) ;

void session_process_exit(struct task_struct *task);

void session_process_abort(struct task_struct *task);

__must_check
int session_console_acquire(int steal) ;

__must_check
int session_console_release(void) ;

__must_check
int session_console_get_foreground_pgid(pid_t *pgid) ;

__must_check
int session_console_set_foreground_pgid(pid_t pgid) ;

__must_check
int session_console_get_sid(pid_t *sid) ;

int session_console_deliver_foreground_signal(int sig);

#endif
