/*
 * mm/mmap.c - 用户地址空间管理
 *
 * 功能：
 *   管理用户进程的虚拟地址空间。mm_struct 包含 pgd、brk、代码段范围
 *   以及 vma[16] 静态数组（最多 16 个 VMA）。每个 vm_area_struct 包含
 *   start、end、flags、used 字段。
 *
 * 数据结构：
 *   mm_struct {pgd, brk, code_start, code_end, vma[16]}
 *   vm_area_struct {start, end, flags, used}
 *
 * 主要函数：
 *   sys_brk(addr)  - brk 系统调用实现。不允许缩小堆，采用延迟分配策略
 *                    （仅更新 brk 指针，不立即分配物理页）。
 *   sys_mmap(addr, len, prot, flags, fd, offset) - mmap 系统调用。
 *                    仅支持 MAP_ANONYMOUS，创建 VMA 但不立即分配物理页，
 *                    实际分配由缺页处理完成。
 *
 * 说明：
 *   VMA 使用固定大小数组而非链表/红黑树，简化实现。
 */
