/*
 * kernel/task.c - 进程控制块管理
 *
 * 功能：
 *   负责 task_struct 的分配、初始化与回收。task_struct 是内核中
 *   最重要的数据结构之一，包含进程的所有状态信息（调度、内存、文件、
 *   信号等）。
 *
 *   idle 进程（PID 0）在 BSS 段中静态分配，不经过动态分配路径。
 *   其余进程通过 SLAB 分配器动态分配 task_struct。
 *
 *   每个进程拥有 8KB 内核栈，栈底写入 CANARY_MAGIC 魔数用于
 *   栈溢出检测，调度切换时校验 canary 完整性。
 *
 * 主要函数：
 *   task_alloc()        - 从 SLAB cache 分配一个新的 task_struct，
 *                         初始化各字段为默认值，分配 8KB 内核栈，
 *                         在栈底写入 CANARY_MAGIC。
 *   task_free(task)     - 释放 task_struct 及其内核栈回 SLAB cache。
 *   kernel_thread(fn, arg, flags) - 创建内核线程。分配 task_struct，
 *                         设置入口函数和参数，初始化内核栈布局
 *                         （switch_to 上下文帧），将线程加入就绪队列。
 */
