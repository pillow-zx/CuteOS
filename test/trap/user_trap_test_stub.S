

	.section .text
	.align 2
	.globl user_trap_test_start
	.globl user_trap_test_illegal
	.globl user_trap_test_handler
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

user_trap_test_illegal:
	.word   0xffffffff
	li      a0,  0x0707070707070707

user_trap_test_ecall:
	ecall

1:
	j       1b

	.align 2
user_trap_test_handler:
	li      t0, 4
	bne     a0, t0, 2f
	lw      t0, 0(a1)
	li      t1, 4
	bne     t0, t1, 2f
	lw      t0, 8(a1)
	li      t1, 1
	bne     t0, t1, 2f
	ld      t0, 16(a1)
	ld      t1, 176(a2)
	bne     t0, t1, 2f
	addi    t1, t1, 4
	sd      t1, 176(a2)
	ret
2:
	li      a0, -1
	ecall
	j       2b

user_trap_test_end:
