#ifndef _CUTEOS_KERNEL_RESOURCE_H
#define _CUTEOS_KERNEL_RESOURCE_H

#include <uapi/resource.h>

void rlimits_init(struct rlimit64 rlimits[RLIM_NLIMITS]);

#endif
