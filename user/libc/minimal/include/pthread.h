#ifndef _PTHREAD_H
#define _PTHREAD_H

#include <user.h>

#ifndef ESRCH
#define ESRCH 3
#endif
#ifndef EDEADLK
#define EDEADLK 35
#endif
#ifndef EBUSY
#define EBUSY 16
#endif

typedef unsigned long pthread_t;

typedef struct {
	int __unused;
} pthread_attr_t;

typedef struct {
	volatile int value;
} pthread_mutex_t;

#define PTHREAD_MUTEX_INITIALIZER { .value = 0 }

int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
		   void *(*start_routine)(void *), void *arg);
int pthread_join(pthread_t thread, void **retval);
int pthread_detach(pthread_t thread);
pthread_t pthread_self(void);
void pthread_exit(void *retval) __attribute__((noreturn));

int pthread_mutex_init(pthread_mutex_t *mutex, const void *attr);
int pthread_mutex_lock(pthread_mutex_t *mutex);
int pthread_mutex_unlock(pthread_mutex_t *mutex);
int pthread_mutex_destroy(pthread_mutex_t *mutex);

#endif
