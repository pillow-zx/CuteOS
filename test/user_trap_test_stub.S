/*
 * init/user_trap_test_stub.S - 3.1 运行时用户态 trap 自测 stub
 *
 * 该代码会被复制到一页临时用户页中，然后通过 schedule() 首次切入：
 *   1. 填充多数通用寄存器为唯一哨兵值
 *   2. 执行 ecall，触发真实的 U->S trap
 *
 * trap_handler 中的测试 hook 会检查 trap_frame 保存结果，
 * 然后把返回目标改写为一个内核 continuation，再切回 idle 上下文。
 *
 * gp (x3) 和 tp (x4) 故意不加载哨兵值：
 *   gp — 用于 GP 相对寻址，加载假值会导致访问异常
 *   tp — 保存线程指针 (TLS)，覆写会破坏 TP 相对访问
 *
 * NOTE: li 立即数必须与 init/user_trap_test.c 中的 UTEST_* 宏保持同步，
 *       修改其中一处时必须同步更新另一处。
 */

	.section .text
	.align 2
	.globl user_trap_test_start
	.globl user_trap_test_ecall
	.globl user_trap_test_end

user_trap_test_start:
	li      ra,  0x0101010101010101
	li      t0,  0x0202020202020202
	li      t1,  0x0303030303030303
	li      t2,  0x0404040404040404
	li      s0,  0x0505050505050505
	li      s1,  0x0606060606060606
	li      a0,  0x0707070707070707
	li      a1,  0x0808080808080808
	li      a2,  0x0909090909090909
	li      a3,  0x0a0a0a0a0a0a0a0a
	li      a4,  0x0b0b0b0b0b0b0b0b
	li      a5,  0x0c0c0c0c0c0c0c0c
	li      a6,  0x0d0d0d0d0d0d0d0d
	li      a7,  0x0e0e0e0e0e0e0e0e
	li      s2,  0x0f0f0f0f0f0f0f0f
	li      s3,  0x1010101010101010
	li      s4,  0x1111111111111111
	li      s5,  0x1212121212121212
	li      s6,  0x1313131313131313
	li      s7,  0x1414141414141414
	li      s8,  0x1515151515151515
	li      s9,  0x1616161616161616
	li      s10, 0x1717171717171717
	li      s11, 0x1818181818181818
	li      t3,  0x1919191919191919
	li      t4,  0x1a1a1a1a1a1a1a1a
	li      t5,  0x1b1b1b1b1b1b1b1b
	li      t6,  0x1c1c1c1c1c1c1c1c

user_trap_test_ecall:
	ecall

1:
	j       1b

user_trap_test_end:
