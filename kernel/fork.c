/*
 * kernel/fork.c - 进程创建（完整物理复制）
 *
 * 功能：
 *   实现 sys_fork 系统调用。通过完整物理复制创建子进程——
 *   为子进程的每个 VMA 分配新的物理页并复制父进程的页面内容。
 *   这是朴素但正确的 fork 语义，后续可引入 COW 优化。
 *
 *   fork 的复制策略：
 *     - 用户页表 + 物理页：按 VMA 逐一复制，子进程获得独立的物理页副本。
 *     - 文件描述符表（fd_array）：复制时对每个打开文件执行 refcount++，
 *       父子共享 file 描述。
 *     - 信号处理器（sighand）：复制 sigaction 数组，父子共享 sighand_struct。
 *     - trap_frame：完整复制父进程的 trap_frame，但将子进程的 a0 寄存器
 *       设为 0，使子进程从 fork 返回时得到返回值 0。
 *     - mm_struct 与 VMA 数组：深拷贝，子进程拥有独立的地址空间描述。
 *     - 待处理信号（pending）：不复制，子进程的 pending 位图清零。
 *
 *   调度策略：fork 返回后父进程先运行，子进程排在就绪队列尾部。
 *
 * 主要函数：
 *   sys_fork()          - fork 系统调用入口，调用 do_fork 创建子进程，
 *                         父进程返回子进程 PID。
 *   copy_mm(oldmm)      - 完整复制父进程的用户地址空间：
 *                         分配新 pgd，遍历 VMA 逐一复制物理页并建立映射。
 *   copy_files(oldfs)   - 复制文件描述符表，对每个已打开 file 执行 refcount++。
 *   copy_sighand(oldsig)- 复制信号处理器表（sigaction 数组）。
 *   copy_trap_frame(tf) - 复制 trap_frame，子进程 a0 = 0。
 */
