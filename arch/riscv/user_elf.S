/*
 * arch/riscv/user_elf.S - 内嵌用户态 ELF 可执行文件
 *
 * 使用 .incbin 将用户态测试程序（ELF64 格式）直接嵌入内核镜像。
 * 导出 _user_init_start / _user_init_end 符号供 C 代码访问。
 *
 * 使用方式：
 *   extern char _user_init_start[], _user_init_end[];
 *   size_t size = _user_init_end - _user_init_start;
 *   exec_user_elf(_user_init_start, size);
 *
 * 注意：Stage 5 实现 EXT2 磁盘加载后，此文件可删除。
 */

	.section .rodata
	.globl _user_init_start
	.globl _user_init_end
_user_init_start:
	.incbin "build/user/init.elf"
_user_init_end:
