/*
 * kernel/init_process.c - PID 1 init 进程
 *
 * 功能：
 *   init 进程是内核创建的第一个用户态进程的祖先。它由 kernel_main
 *   通过 kernel_thread() 创建，永不退出。主要职责：
 *   1. Stage 3~4：execve 内嵌测试用户程序验证 fork/exec/wait
 *   2. Stage 5+：fork 出 /bin/sh，然后常驻 wait(-1) 循环回收孤儿进程
 *   3. 如果 shell 退出，重新 fork 一个新的 shell
 *
 * 注意事项：
 *   init 是内核线程（入口为内核函数），在内部调用 execve 变为用户进程。
 *   init 永远不返回，其入口函数是死循环。
 */
