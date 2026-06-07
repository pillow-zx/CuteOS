/*
 * kernel/exit.c - 进程退出与回收
 *
 * 功能：
 *   处理进程终止请求和相关系统调用。当进程调用 exit 或收到致命信号时，
 *   逐步释放其占用的资源，最终由父进程调用 wait4 完成 task_struct 回收。
 *   若父进程先于子进程退出，子进程会被过继（reparent）给 init 进程。
 *
 *   do_exit 执行流程：
 *     1. 将进程状态设为 ZOMBIE。
 *     2. 关闭所有已打开的文件描述符（close all fds）。
 *     3. 释放用户地址空间（mm_struct、VMA、物理页）。
 *     4. 将当前进程的子进程过继给 init 进程（reparent orphans to init）。
 *     5. 向父进程发送 SIGCHLD 信号通知。
 *     6. 调用 schedule() 切换到下一个可运行进程，永不返回。
 *
 *   sys_wait4 支持：
 *     - pid > 0 ：等待指定 PID 的子进程。
 *     - pid == -1：等待任意子进程（等价于 waitpid(-1, ...)）。
 *     - 使用 sleep_on/wake_up 在子进程等待队列上睡眠/唤醒。
 *
 * 主要函数：
 *   do_exit(code)               - 核心退出逻辑：设置 ZOMBIE 状态，
 *                                 关闭所有 fd，释放用户空间，
 *                                 过继孤儿给 init，发送 SIGCHLD 给父进程，
 *                                 调用 schedule() 永不返回。
 *   sys_wait4(pid, wstatus, options, rusage) - 等待子进程状态变化，
 *                                 支持 pid>0 和 pid==-1，
 *                                 使用 sleep_on 在子进程等待队列上阻塞。
 *   release_task(task)          - 最终的 task_struct 回收（供 wait4 调用）。
 *   reparent_children(dead_task)- 将死进程的子进程过继给 init 进程。
 */
