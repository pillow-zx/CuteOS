#ifndef _CUTEOS_KERNEL_MM_H
#define _CUTEOS_KERNEL_MM_H

/*
 * include/kernel/mm.h - 用户地址空间管理与虚拟内存
 *
 * 声明进程级虚拟内存管理所需的结构体与常量。每个进程拥有一个
 * mm_struct 描述其完整地址空间，内部划分为若干 vm_area_struct 区域。
 *
 * Structs:
 *   struct mm_struct      - Per-process address space (page table root,
 *                           VMA list, brk, stack pointers)
 *   struct vm_area_struct - One contiguous virtual memory region
 *                           (vm_start, vm_end, vm_flags, vm_next)
 *
 * VMA permission flags:
 *   VM_READ  - Page is readable
 *   VM_WRITE - Page is writable
 *   VM_EXEC  - Page is executable
 *
 * Constants:
 *   NR_VMA = 16  (max VMA regions per process)
 *
 * mmap flags:
 *   MAP_ANONYMOUS, MAP_PRIVATE, MAP_FIXED, MAP_SHARED
 */

#endif
