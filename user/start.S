/*
 * user/start.S - 用户程序入口
 */

#include <uapi/syscall.h>

	.globl _start
	.section .text.entry
_start:

	.option push
	.option norelax
1:	auipc	gp, %pcrel_hi(__global_pointer$)
	addi	gp, gp, %pcrel_lo(1b)
	.option pop


	li	s0, 0


	ld	a0, 0(sp)
	addi	a1, sp, 8
	slli	t0, a0, 3
	add	a2, a1, t0
	addi	a2, a2, 8
	call	main


	li	a7, SYS_exit
	ecall
