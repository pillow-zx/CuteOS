/*
 * mm/page_fault.c - 缺页异常处理
 *
 * 功能：
 *   处理由 MMU 触发的缺页异常（Page Fault）。仅处理两种情况：
 *   - 合法缺页（legal fault）：虚拟地址落在合法 VMA 内，分配物理页
 *     并建立映射。
 *   - 非法访问：向进程发送 SIGSEGV。
 *
 *   不支持 COW（写时复制）和栈自动扩展。
 *
 * 内核态缺页：
 *   支持内核态下由 copy_to_user / copy_from_user 触发的缺页，
 *   此时自动为对应的用户页分配物理页（而非发送信号）。
 *
 * 主要函数：
 *   do_page_fault(tf) - 缺页异常总入口。读取 stval 获取出错虚拟地址，
 *             读取 scause 区分读/写缺页类型，查找 VMA 判断合法性，
 *             调用对应处理（分配页面或发送 SIGSEGV）。
 */
