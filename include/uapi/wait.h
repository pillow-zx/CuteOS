#ifndef _CUTEOS_UAPI_WAIT_H
#define _CUTEOS_UAPI_WAIT_H

/**
 * @file wait.h
 * @brief Linux wait4(2) option constants.
 */

#define WNOHANG	   0x00000001
#define WUNTRACED  0x00000002
#define WSTOPPED   WUNTRACED
#define WCONTINUED 0x00000008

_Static_assert(WNOHANG == 1, "WNOHANG ABI value mismatch");
_Static_assert(WUNTRACED == 2, "WUNTRACED ABI value mismatch");
_Static_assert(WCONTINUED == 8, "WCONTINUED ABI value mismatch");

#endif
