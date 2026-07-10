#ifndef _CUTEOS_KERNEL_EVENTPOLL_H
#define _CUTEOS_KERNEL_EVENTPOLL_H

/**
 * @file eventpoll.h
 * @brief VFS-backed eventpoll file implementation.
 */

#include <kernel/compiler.h>
#include <kernel/types.h>
#include <uapi/eventpoll.h>

struct file;
struct wait_deadline;

struct file *__must_check eventpoll_file_alloc(void);
bool __must_check eventpoll_file(struct file *file);
int __must_check eventpoll_ctl(struct file *epfile, int op, int fd,
			       struct file *file,
			       const struct epoll_event *event);
int __must_check eventpoll_wait(struct file *epfile,
				struct epoll_event *events, int maxevents,
				const struct wait_deadline *deadline);

#endif
