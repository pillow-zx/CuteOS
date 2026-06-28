#ifndef _CUTEOS_KERNEL_ELF_H
#define _CUTEOS_KERNEL_ELF_H

/*
 * include/kernel/elf.h - ELF64 格式定义
 *
 * 定义 ELF64 可执行文件加载所需的数据结构与常量。
 * 仅支持 64 位小端序 RISC-V ELF 可执行文件。
 *
 * 参考：ELF-64 Object File Format (Tool Interface Standards)
 */

#include <kernel/types.h>

/* ---- ELF Magic ---- */

#define ELFMAG0		0x7f
#define ELFMAG1		'E'
#define ELFMAG2		'L'
#define ELFMAG3		'F'
#define ELFMAG		"\x7f""ELF"
#define SELFMAG		4

/* e_ident[] 索引 */
#define EI_MAG0	      0
#define EI_MAG1	      1
#define EI_MAG2	      2
#define EI_MAG3	      3
#define EI_CLASS      4
#define EI_DATA	      5
#define EI_VERSION    6
#define EI_OSABI      7
#define EI_ABIVERSION 8
#define EI_PAD	      9
#define EI_NIDENT     16

/* EI_CLASS */
#define ELFCLASSNONE 0
#define ELFCLASS32   1
#define ELFCLASS64   2

/* EI_DATA */
#define ELFDATANONE 0
#define ELFDATA2LSB 1 /* 小端序 */
#define ELFDATA2MSB 2 /* 大端序 */

/* e_type */
#define ET_NONE		0
#define ET_REL		1
#define ET_EXEC		2
#define ET_DYN		3
#define ET_CORE		4

/* e_machine */
#define EM_NONE		0
#define EM_RISCV	243

/* e_version / EV_CURRENT */
#define EV_NONE		0
#define EV_CURRENT	1

/* ---- ELF64 文件头 ---- */

typedef struct {
	unsigned char e_ident[EI_NIDENT]; /* ELF 标识 */
	uint16_t e_type;		          /* 目标文件类型 */
	uint16_t e_machine;		          /* 架构 */
	uint32_t e_version;		          /* 目标文件版本 */
	uint64_t e_entry;		          /* 入口点虚拟地址 */
	uint64_t e_phoff;		          /* Program header 表偏移 */
	uint64_t e_shoff;		          /* Section header 表偏移 */
	uint32_t e_flags;		          /* 处理器特定标志 */
	uint16_t e_ehsize;		          /* ELF 头大小 */
	uint16_t e_phentsize;		      /* Program header 表项大小 */
	uint16_t e_phnum;		          /* Program header 表项数量 */
	uint16_t e_shentsize;		      /* Section header 表项大小 */
	uint16_t e_shnum;		          /* Section header 表项数量 */
	uint16_t e_shstrndx;		      /* Section header 字符串表索引 */
} Elf64_Ehdr;

/* ---- Program header 段类型 ---- */

#define PT_NULL		0	/* 未使用 */
#define PT_LOAD		1	/* 可加载段 */
#define PT_DYNAMIC	2	/* 动态链接信息 */
#define PT_INTERP	3	/* 解释器路径 */
#define PT_NOTE		4	/* 附注 */
#define PT_SHLIB	5
#define PT_PHDR		6
#define PT_TLS		7

/* p_flags 权限位 */
#define PF_X		0x1	/* 可执行 */
#define PF_W		0x2	/* 可写 */
#define PF_R		0x4	/* 可读 */

/* ---- ELF64 Program header ---- */

typedef struct {
	uint32_t p_type;   /* 段类型 */
	uint32_t p_flags;  /* 段权限标志 */
	uint64_t p_offset; /* 段在文件中的偏移 */
	uint64_t p_vaddr;  /* 段的虚拟地址 */
	uint64_t p_paddr;  /* 段的物理地址（未使用） */
	uint64_t p_filesz; /* 段在文件中的大小 */
	uint64_t p_memsz;  /* 段在内存中的大小 */
	uint64_t p_align;  /* 段对齐 */
} Elf64_Phdr;

#endif
