/*
 * syscall/sys_stub.c - probe-safe or unsupported syscall placeholders
 *
 * Keep this file for ABI entries that intentionally do not yet delegate to a
 * core subsystem. Syscalls backed by real subsystem behavior should live in a
 * named syscall/sys_*.c owner file instead.
 */
