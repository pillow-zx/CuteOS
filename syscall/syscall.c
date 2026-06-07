/*
 * syscall/syscall.c - 系统调用分发
 *
 * 功能：
 *   管理系统调用分发表（syscall_table）。使用 typedef long (*syscall_fn_t)(uint64_t x 6)
 *   定义统一的系统调用函数签名。从 trap_frame 的 a7 寄存器读取系统调用号，
 *   以 a0~a5 作为参数调用 syscall_table[nr]。返回值写入 tf->a0。
 *   未知的系统调用号返回 -ENOSYS。当前共约 34 个系统调用。
 *
 * 主要函数：
 *   do_syscall(tf)         - 读取 a7 获取系统调用号，syscall_table[nr](a0..a5)，
 *                            返回结果写入 tf->a0，未知号返回 -ENOSYS
 *   syscall_init()         - 初始化 syscall_table，注册约 34 个系统调用
 *
 * 关键类型：
 *   syscall_fn_t           - typedef long (*syscall_fn_t)(uint64_t x 6)
 */
