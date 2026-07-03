	.text
	.align 2

	.globl __ctzdi2
	.type __ctzdi2, @function
__ctzdi2:
	li a1, 0
	li t0, 0xffffffff
	and t1, a0, t0
	bnez t1, 1f
	addi a1, a1, 32
	srli a0, a0, 32
1:
	slli t1, a0, 48
	bnez t1, 2f
	addi a1, a1, 16
	srli a0, a0, 16
2:
	andi t1, a0, 0xff
	bnez t1, 3f
	addi a1, a1, 8
	srli a0, a0, 8
3:
	andi t1, a0, 0xf
	bnez t1, 4f
	addi a1, a1, 4
	srli a0, a0, 4
4:
	andi t1, a0, 0x3
	bnez t1, 5f
	addi a1, a1, 2
	srli a0, a0, 2
5:
	andi t1, a0, 0x1
	bnez t1, 6f
	addi a1, a1, 1
6:
	mv a0, a1
	ret

	.globl __clzdi2
	.type __clzdi2, @function
__clzdi2:
	li a1, 0
	srli t0, a0, 32
	bnez t0, 1f
	addi a1, a1, 32
	j 2f
1:
	mv a0, t0
2:
	srli t0, a0, 16
	bnez t0, 3f
	addi a1, a1, 16
	j 4f
3:
	mv a0, t0
4:
	srli t0, a0, 8
	bnez t0, 5f
	addi a1, a1, 8
	j 6f
5:
	mv a0, t0
6:
	srli t0, a0, 4
	bnez t0, 7f
	addi a1, a1, 4
	j 8f
7:
	mv a0, t0
8:
	srli t0, a0, 2
	bnez t0, 9f
	addi a1, a1, 2
	j 10f
9:
	mv a0, t0
10:
	srli t0, a0, 1
	bnez t0, 11f
	addi a1, a1, 1
11:
	mv a0, a1
	ret

	.globl __ffsdi2
	.type __ffsdi2, @function
__ffsdi2:
	beqz a0, 1f
	addi sp, sp, -16
	sd ra, 8(sp)
	call __ctzdi2
	ld ra, 8(sp)
	addi sp, sp, 16
	addi a0, a0, 1
	ret
1:
	ret

	.globl ffs
	.type ffs, @function
ffs:
	sext.w a0, a0
	beqz a0, 6f
	li a1, 1
	slli t0, a0, 48
	bnez t0, 1f
	addi a1, a1, 16
	srli a0, a0, 16
1:
	andi t0, a0, 0xff
	bnez t0, 2f
	addi a1, a1, 8
	srli a0, a0, 8
2:
	andi t0, a0, 0xf
	bnez t0, 3f
	addi a1, a1, 4
	srli a0, a0, 4
3:
	andi t0, a0, 0x3
	bnez t0, 4f
	addi a1, a1, 2
	srli a0, a0, 2
4:
	andi t0, a0, 0x1
	bnez t0, 5f
	addi a1, a1, 1
5:
	mv a0, a1
	ret
6:
	ret
