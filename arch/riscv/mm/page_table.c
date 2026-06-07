/*
 * arch/riscv/mm/page_table.c - Sv39 三级页表操作
 *
 * 功能：
 *   实现 RISC-V Sv39 分页机制的页表创建、映射和地址转换。
 *   Sv39 使用三级页表（PGD → PMD → PTE），每级 9 bits 索引，
 *   页大小 4KB，虚拟地址高 25 位为符号扩展，虚拟地址空间 512GB。
 *
 * 主要函数：
 *   kernel_pagetable_init() - 初始化内核全局页表。两步映射：
 *     步骤一：为 256MB DRAM 物理内存建立 4KB 细粒度页映射
 *             (恒等映射 + 高地址映射)，权限为 R+W+X（内核代码/数据）。
 *     步骤二：pgd[0] 建立 1GB mega page 映射 MMIO 设备空间
 *             (0x10000000 区域)，权限为 R+W（设备寄存器不可执行）。
 *
 *   map_page(pgtbl, va, pa, perm) - 建立单个 4KB 页的虚拟地址到物理地址映射。
 *             自动分配中间页表页（PMD），无需预先创建。
 *
 *   walk_page_table(pgtbl, va) - 三级页表遍历。从 PGD → PMD → PTE 逐级查找，
 *             返回最终 PTE 的指针（虚拟地址）。若中间级页表不存在则分配新页。
 *             是 map_page / unmap_page / va_to_pa 的核心实现。
 *
 * Sv39 地址分解（4KB 页）：
 *   [63:39] 符号扩展  [38:30] PGD 索引  [29:21] PMD 索引
 *   [20:12] PTE 索引  [11:0]  页内偏移
 *
 * 注意事项：
 *   - 页表页由物理内存分配器 (buddy) 分配，必须 4KB 对齐
 *   - PTE 权限位遵循 RISC-V 特权规范：V | R | W | X | U | G | A | D
 */
