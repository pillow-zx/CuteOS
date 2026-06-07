/*
 * lib/dtb.c - 设备树 DTB 解析（Stage 7 可选，占位）
 *
 * 功能：
 *   解析 Flattened Device Tree（FDT/DTB）二进制数据，从中提取系统
 *   硬件配置信息。当前为 Stage 7 可选功能的占位实现。DTB 的起始
 *   地址由 OpenSBI 通过 a1 寄存器传递给内核。
 *
 * 主要函数：
 *   dtb_parse(dtb_addr)       - 解析 DTB 数据，遍历节点树（占位）
 *   dtb_find_memory(dtb_addr) - 在 DTB 中查找 /memory 节点（占位）
 */
