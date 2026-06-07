/*
 * kernel/wait.c - 等待队列与睡眠原语
 *
 * 功能：
 *   实现进程睡眠和唤醒机制，基于等待队列（wait_queue_head_t）。
 *   当进程需要等待某个条件满足时，将自己加入等待队列并进入睡眠状态，
 *   条件满足时由唤醒方将队列中的进程移回就绪队列。
 *
 *   典型使用场景：
 *     - pipe 读写阻塞
 *     - sys_wait4 等待子进程
 *     - console read 等待输入
 *
 * 主要函数：
 *   sleep_on(wq)       - 将当前进程加入等待队列 wq，设置状态为 SLEEPING，
 *                        调用 schedule() 让出 CPU。被唤醒后从队列移除。
 *   wake_up(wq)        - 唤醒等待队列 wq 上的第一个进程，
 *                        将其状态设为 RUNNING 并加入就绪队列。
 *   wake_up_all(wq)    - 唤醒等待队列 wq 上的全部进程。
 */
