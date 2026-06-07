/*
 * syscall/sys_mm.c - 内存相关系统调用
 *
 * 功能：
 *   实现进程地址空间操作的系统调用。brk 不允许缩小堆，使用惰性分配。
 *   mmap 仅支持 MAP_ANONYMOUS 映射，使用惰性分配（缺页时才实际分配
 *   物理页）。munmap 解除内存映射。
 *
 * 主要函数：
 *   sys_brk(addr)                           - 调整堆边界，不缩小，惰性分配
 *   sys_mmap(addr, len, prot, flags, ...)   - 创建匿名映射（MAP_ANONYMOUS
 * only， 惰性分配，缺页时分配物理页） sys_munmap(addr, len)                   -
 * 解除内存映射
 */
