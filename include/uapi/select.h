#ifndef _CUTEOS_UAPI_SELECT_H
#define _CUTEOS_UAPI_SELECT_H

#define __FD_SETSIZE 1024
#define __NFDBITS    (8 * (int)sizeof(unsigned long))

typedef struct {
	unsigned long fds_bits[__FD_SETSIZE / __NFDBITS];
} fd_set;

struct pselect6_sigmask {
	const unsigned long *ss;
	unsigned long ss_len;
};

#endif
