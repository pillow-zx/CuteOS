#ifndef _CUTEOS_KERNEL_TTY_INTERNAL_H
#define _CUTEOS_KERNEL_TTY_INTERNAL_H

#include <kernel/compiler.h>
#include <kernel/types.h>

struct task_struct;
struct tty_endpoint;

struct tty_ctty_state {
	pid_t sid;
	pid_t foreground_pgid;
};

enum tty_ctty_remove_scope {
	TTY_CTTY_REMOVE_TASK,
	TTY_CTTY_REVOKE_SESSION,
};

void tty_console_endpoint_init(void);
struct tty_endpoint *tty_console_endpoint(void) __must_check;
int tty_ctty_clone_attachment(struct task_struct *parent,
			      struct task_struct *child) __must_check;
void tty_ctty_remove_task(struct task_struct *task, pid_t sid,
			  enum tty_ctty_remove_scope scope,
			  struct tty_ctty_state *detached);
bool tty_ctty_has_owner(struct tty_endpoint *tty, struct tty_ctty_state *state);
int tty_ctty_claim(struct tty_endpoint *tty, struct task_struct *task,
		   pid_t sid, pid_t pgid, bool steal,
		   struct tty_ctty_state *displaced) __must_check;
bool tty_ctty_owned_by(struct tty_endpoint *tty, struct task_struct *task,
		       pid_t sid);
int tty_ctty_get_foreground_pgid(struct tty_endpoint *tty,
				 struct task_struct *task, pid_t sid,
				 pid_t *pgid) __must_check;
int tty_ctty_set_foreground_pgid(struct tty_endpoint *tty,
				 struct task_struct *task, pid_t sid,
				 pid_t pgid) __must_check;
int tty_ctty_snapshot_foreground(struct tty_endpoint *tty,
				 struct tty_ctty_state *state) __must_check;
void tty_ctty_clear_foreground_if(pid_t sid, pid_t pgid);

#endif
