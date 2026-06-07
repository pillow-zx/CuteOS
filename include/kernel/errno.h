#ifndef _CUTEOS_KERNEL_ERRNO_H
#define _CUTEOS_KERNEL_ERRNO_H

/*
 * include/kernel/errno.h - POSIX 错误码常量
 *
 * 定义内核中用于系统调用返回值和内部错误报告的错误码。
 * 数值与标准 Linux errno 编号一致。
 */

 #define   EPERM     1
 #define   ENOENT    2
 #define   ESRCH     3
 #define   EINTR     4
 #define   EIO       5
 #define   ENXIO     6
 #define   E2BIG     7
 #define   ENOEXEC   8
 #define   EBADF     9
 #define   ECHILD    10
 #define   EAGAIN    11
 #define   ENOMEM    12
 #define   EACCES    13
 #define   EFAULT    14
 #define   EINVAL    22
 #define   ENFILE    23
 #define   EMFILE    24
 #define   ENOTTY    25
 #define   ENOSPC    28
 #define   ESPIPE    29
 #define   EROFS     30
 #define   ENOSYS    38
 #define   ENOTEMPTY 39

#endif
