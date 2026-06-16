/*
 * user/start.S - 用户程序入口
 *
 * _start 是用户程序的第一个执行点。
 * 初始栈布局由内核 exec 构造：
 *   sp + 0: argc
 *   sp + 8: argv[0]
 *   ...    argv NULL, envp[0], envp NULL
 * 清零帧指针后调用 main(argc, argv, envp)，返回值作为 exit 状态码。
 */

	.globl _start
	.section .text.entry
_start:
	/* 初始化 gp，支持链接器未来对 .sdata/.sbss 做 GP-relative relaxation。 */
	.option push
	.option norelax
1:	auipc	gp, %pcrel_hi(__global_pointer$)
	addi	gp, gp, %pcrel_lo(1b)
	.option pop

	/* 清零 fp (s0)，标记栈回溯终点 */
	li	s0, 0

	/* 调用 main(argc, argv, envp) */
	ld	a0, 0(sp)
	addi	a1, sp, 8
	slli	t0, a0, 3
	add	a2, a1, t0
	addi	a2, a2, 8
	call	main

	/* main 返回值作为 exit 状态码 */
	li	a7, 93			/* SYS_exit */
	ecall
