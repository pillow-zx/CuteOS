	.text
	.align 2

	.globl memcpy
	.type memcpy, @function
memcpy:
	mv t0, a0
	beqz a2, memcpy_done
memcpy_loop:
	lbu t1, 0(a1)
	sb t1, 0(a0)
	addi a0, a0, 1
	addi a1, a1, 1
	addi a2, a2, -1
	bnez a2, memcpy_loop
memcpy_done:
	mv a0, t0
	ret

	.globl memset
	.type memset, @function
memset:
	mv t0, a0
	andi t1, a1, 0xff
	beqz a2, memset_done
memset_loop:
	sb t1, 0(a0)
	addi a0, a0, 1
	addi a2, a2, -1
	bnez a2, memset_loop
memset_done:
	mv a0, t0
	ret

	.globl memcmp
	.type memcmp, @function
memcmp:
	beqz a2, memcmp_equal
memcmp_loop:
	lbu t0, 0(a0)
	lbu t1, 0(a1)
	bne t0, t1, memcmp_diff
	addi a0, a0, 1
	addi a1, a1, 1
	addi a2, a2, -1
	bnez a2, memcmp_loop
memcmp_equal:
	li a0, 0
	ret
memcmp_diff:
	sub a0, t0, t1
	ret

	.globl memmove
	.type memmove, @function
memmove:
	mv t0, a0
	beq a0, a1, memmove_done
	beqz a2, memmove_done
	bltu a0, a1, memmove_forward
	add t1, a0, a2
	add t2, a1, a2
memmove_backward_loop:
	addi t1, t1, -1
	addi t2, t2, -1
	lbu t3, 0(t2)
	sb t3, 0(t1)
	addi a2, a2, -1
	bnez a2, memmove_backward_loop
	j memmove_done
memmove_forward:
	mv t1, a0
	mv t2, a1
memmove_forward_loop:
	lbu t3, 0(t2)
	sb t3, 0(t1)
	addi t1, t1, 1
	addi t2, t2, 1
	addi a2, a2, -1
	bnez a2, memmove_forward_loop
memmove_done:
	mv a0, t0
	ret

	.globl strlen
	.type strlen, @function
strlen:
	mv t0, a0
strlen_loop:
	lbu t1, 0(a0)
	beqz t1, strlen_done
	addi a0, a0, 1
	j strlen_loop
strlen_done:
	sub a0, a0, t0
	ret

	.globl strnlen
	.type strnlen, @function
strnlen:
	mv t0, a0
strnlen_loop:
	beqz a1, strnlen_done
	lbu t1, 0(a0)
	beqz t1, strnlen_done
	addi a0, a0, 1
	addi a1, a1, -1
	j strnlen_loop
strnlen_done:
	sub a0, a0, t0
	ret

	.globl strcmp
	.type strcmp, @function
strcmp:
strcmp_loop:
	lbu t0, 0(a0)
	lbu t1, 0(a1)
	bne t0, t1, strcmp_diff
	beqz t0, strcmp_equal
	addi a0, a0, 1
	addi a1, a1, 1
	j strcmp_loop
strcmp_equal:
	li a0, 0
	ret
strcmp_diff:
	sub a0, t0, t1
	ret

	.globl strncmp
	.type strncmp, @function
strncmp:
strncmp_loop:
	beqz a2, strncmp_equal
	lbu t0, 0(a0)
	lbu t1, 0(a1)
	bne t0, t1, strncmp_diff
	beqz t0, strncmp_equal
	addi a0, a0, 1
	addi a1, a1, 1
	addi a2, a2, -1
	j strncmp_loop
strncmp_equal:
	li a0, 0
	ret
strncmp_diff:
	sub a0, t0, t1
	ret

	.globl strcpy
	.type strcpy, @function
strcpy:
	mv t0, a0
strcpy_loop:
	lbu t1, 0(a1)
	sb t1, 0(a0)
	addi a0, a0, 1
	addi a1, a1, 1
	bnez t1, strcpy_loop
	mv a0, t0
	ret

	.globl strncpy
	.type strncpy, @function
strncpy:
	mv t0, a0
strncpy_copy_loop:
	beqz a2, strncpy_done
	lbu t1, 0(a1)
	beqz t1, strncpy_zero_fill
	sb t1, 0(a0)
	addi a0, a0, 1
	addi a1, a1, 1
	addi a2, a2, -1
	j strncpy_copy_loop
strncpy_zero_fill:
	sb zero, 0(a0)
	addi a0, a0, 1
	addi a2, a2, -1
	bnez a2, strncpy_zero_fill
strncpy_done:
	mv a0, t0
	ret

	.globl strchr
	.type strchr, @function
strchr:
	andi a1, a1, 0xff
strchr_loop:
	lbu t0, 0(a0)
	beq t0, a1, strchr_found
	beqz t0, strchr_not_found
	addi a0, a0, 1
	j strchr_loop
strchr_found:
	ret
strchr_not_found:
	li a0, 0
	ret

	.globl strrchr
	.type strrchr, @function
strrchr:
	andi a1, a1, 0xff
	li t0, 0
strrchr_loop:
	lbu t1, 0(a0)
	bne t1, a1, strrchr_skip
	mv t0, a0
strrchr_skip:
	beqz t1, strrchr_done
	addi a0, a0, 1
	j strrchr_loop
strrchr_done:
	mv a0, t0
	ret
