/*
 * fs/vfs/dentry.c - Dentry 缓存
 *
 * 功能：
 *   管理 VFS 层的目录项缓存（dcache）。dentry 是路径分量与 inode
 *   之间的桥梁，将文件名与对应的 inode 关联起来。dcache 使用 128 桶
 *   哈希表，以 (parent, name) 为键进行索引。本实现不做驱逐，dentry
 *   创建后永久留在哈希表中。dget/dput 管理引用计数。d_parent 指针
 *   用于 ".." 路径遍历时回溯到父目录。
 *
 * 主要函数：
 *   dget(dentry)             - 增加 dentry 引用计数
 *   dput(dentry)             - 减少引用计数（dentry 仍在哈希表中）
 *   dcache_lookup(parent, name) - 在 128 桶哈希表中查找 dentry
 *   dcache_insert(dentry)    - 将 dentry 插入哈希表
 *   dcache_init()            - 初始化 128 桶哈希表
 *
 * 关键数据结构：
 *   dentry.d_parent          - 指向父 dentry，用于 ".." 遍历
 *   dcache 哈希表            - 128 桶，以 (parent, name) 为键，无驱逐策略
 */
