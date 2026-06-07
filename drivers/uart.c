/*
 * drivers/uart.c - NS16550A UART 驱动
 *
 * 功能：
 *   实现 NS16550A UART（串口）驱动，基地址 0x10000000。作为内核
 *   早期调试和最终控制台的字符设备。支持两个初始化阶段以适配内核
 *   启动过程。
 *
 * 初始化阶段：
 *   console_init_sbi()  - 早期阶段（Stage 1-4），在页表建立前使用
 *             SBI ecall 进行串口访问。
 *   console_init_mmio() - 页表建立后切换到 MMIO 直接访问方式。
 *
 * 主要函数：
 *   uart_putc(ch)  - 输出单个字符。轮询 LSR 寄存器等待 THR 就绪后写入。
 *   uart_getc()    - 读入单个字符。轮询 LSR 寄存器等待 RBR 有数据后读取
 *            （忙等待方式）。早期 Stage 1-4 的读取均使用忙轮询。
 */
