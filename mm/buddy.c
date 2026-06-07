/*
 * mm/buddy.c - 伙伴系统（物理页分配）
 *
 * 功能：
 *   实现经典的伙伴分配算法管理物理内存页框。max_order=9（最大块 2MB），
 *   即 free_area[10]。struct page 包含 flags、order、refcount、lru 字段。
 *   mem_map 数组位于内核映像 _end 之后，覆盖全部物理页。分配失败（OOM）
 *   时返回 NULL。
 *
 * 数据结构：
 *   struct page {flags, order, refcount, lru}
 *   struct free_area {list}  free_area[10]
 *   mem_map[]  - 全局 struct page 数组，紧接 _end
 *
 * 主要函数：
 *   buddy_init(mem_start, mem_end)  - 从 _end 到 DRAM 末尾初始化伙伴系统，
 *                   将可用物理内存区域按 order 加入空闲链表。
 *   get_free_page(order)  - 分配 2^order 个连续物理页，返回首页地址。
 *   free_page(addr, order) - 释放指定地址的页块，并尝试伙伴合并。
 *
 * 合并策略：
 *   释放时计算伙伴地址，若伙伴同样空闲且 order 相同则合并，递归向上
 *   直到无法合并或达到 max_order 为止。
 */
