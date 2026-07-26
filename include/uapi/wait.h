#ifndef _CUTEOS_UAPI_WAIT_H
#define _CUTEOS_UAPI_WAIT_H

/**
 * @file wait.h
 * @brief Linux wait4(2) option constants.
 */

#define WNOHANG 0x00000001

_Static_assert(WNOHANG == 1, "WNOHANG ABI value mismatch");

#endif
