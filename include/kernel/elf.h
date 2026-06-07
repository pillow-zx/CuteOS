#ifndef _CUTEOS_KERNEL_ELF_H
#define _CUTEOS_KERNEL_ELF_H

/*
 * include/kernel/elf.h - ELF64 格式定义，用于可执行文件加载
 *
 * 定义 execve 加载用户程序到内存时使用的 ELF64 数据结构。
 * 仅支持 64 位 RISC-V ELF 可执行文件。
 *
 * Structs:
 *   Elf64_Ehdr - ELF file header (magic, entry point, program header offset)
 *   Elf64_Phdr - ELF program header (segment type, vaddr, file/mem sizes)
 *
 * Constants:
 *   ELF magic check - e_ident[0..3] = { 0x7f, 'E', 'L', 'F' }
 *   PT_LOAD         - Loadable program segment type
 *   ET_EXEC         - Executable file type
 */

#endif
