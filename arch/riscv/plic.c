/*
 * arch/riscv/plic.c - PLIC 外部中断控制器 (占位)
 *
 * 功能：
 *   PLIC（Platform-Level Interrupt Controller）是 RISC-V 平台级中断控制器，
 *   管理所有外部设备中断（如 UART、virtio-blk）。支持多优先级和多 CPU 核。
 *
 * 当前状态：
 *   本文件为占位实现，在 Stage 1~4 阶段不进行 PLIC 初始化。
 *   早期控制台输出通过 SBI ecall (sbi_console_putchar) 完成，
 *   无需 UART 中断驱动。
 *
 * 预期用途（Stage 5+）：
 *   当内核进入 Stage 5 及以后阶段时，本模块将被完整实现，用于：
 *   - UART 中断驱动控制台：替代 SBI 轮询输出，支持输入中断
 *   - virtio-blk 磁盘设备中断
 *   - 其他外部设备中断
 *
 * 主要函数（待实现）：
 *   plic_init()       - 初始化 PLIC：配置优先级阈值，启用 S 模式外部中断
 *   plic_enable(irq)  - 使能指定中断源
 *   plic_claim()      - 获取当前待处理的中断 ID (由 trap_handler 在
 *                       收到 External 中断后调用)
 *   plic_complete(id) - 完成中断处理，通知 PLIC 释放该中断源
 */
