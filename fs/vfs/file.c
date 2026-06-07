/*
 * fs/vfs/file.c - 文件对象管理
 *
 * 功能：
 *   管理进程打开文件对应的 file 对象。file 通过 kmalloc/kfree 分配
 *   和释放。文件描述符从进程的 fd_array[32] 中分配。file 结构包含
 *   inode 指针、操作向量 f_op、读写位置 f_pos、标志 f_flags、
 *   模式 f_mode 和引用计数 refcount。file_operations 操作向量定义
 *   了 read、write、llseek、open、release、readdir 等方法，由
 *   底层文件系统实现。
 *
 * 主要函数：
 *   file_alloc()              - 通过 kmalloc 分配 file 对象
 *   file_free(file)           - 通过 kfree 释放 file 对象
 *   fd_alloc()                - 从 fd_array[32] 中分配空闲文件描述符
 *
 * 关键数据结构：
 *   struct file               - {inode, f_op, f_pos, f_flags, f_mode, refcount}
 *   file_operations           - {read, write, llseek, open, release, readdir}
 *   fd_array[32]              - 进程文件描述符表
 */
