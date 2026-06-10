/*
 * user/start.S - 用户程序入口
 *
 * _start 是用户程序的第一个执行点。
 * 清零帧指针后调用 main(0, NULL)，返回值作为 exit 状态码。
 */

	.globl _start
	.section .text.entry
_start:
	/* 清零 fp (s0)，标记栈回溯终点 */
	li	s0, 0

	/* 调用 main(argc=0, argv=NULL) */
	li	a0, 0
	li	a1, 0
	call	main

	/* main 返回值作为 exit 状态码 */
	li	a7, 93			/* SYS_exit */
	ecall
