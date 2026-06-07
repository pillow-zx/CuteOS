/*
 * kernel/pid.c - PID 分配器
 *
 * 功能：
 *   管理进程 ID（PID）的分配与回收。使用位图（bitmap）跟踪 PID 使用状态，
 *   PID 范围为 0~255，提供 O(1) 的分配和释放操作。
 *
 * 主要函数：
 *   pid_init()         - 初始化 PID 位图（清零全部位），
 *                        预留 PID 0 给 idle 进程（置位 0）。
 *   alloc_pid()        - 从位图中查找第一个为 0 的位，置 1 并返回 PID 值。
 *                        若无可用 PID 则返回错误。
 *   free_pid(pid)      - 清除位图中对应 PID 的位，释放回可用池。
 */
