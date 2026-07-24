#ifndef _CUTEOS_UAPI_AUXVEC_H
#define _CUTEOS_UAPI_AUXVEC_H

/* Linux auxiliary-vector type values used at process entry. */
#define AT_NULL	  0
#define AT_PHDR	  3
#define AT_PHENT  4
#define AT_PHNUM  5
#define AT_PAGESZ 6
#define AT_ENTRY  9
#define AT_UID	  11
#define AT_EUID	  12
#define AT_GID	  13
#define AT_EGID	  14
#define AT_SECURE 23
#define AT_RANDOM 25
#define AT_EXECFN 31

#endif
