/*
 * fs/vfs/namei.c - 路径解析
 *
 * 功能：
 *   实现 VFS 层的路径名解析（Pathname Lookup）。path_lookup 逐分量
 *   解析路径。绝对路径从 root_dentry 开始，相对路径从 current->cwd
 *   开始。每个分量处理 "."（跳过）和 ".."（回溯到 d_parent）。不
 *   支持符号链接跟随。每个分量最终调用 i_op->lookup 在磁盘上查找。
 *
 * 主要函数：
 *   path_lookup(path, flags) - 主路径解析函数。绝对路径从 root_dentry
 *                              开始，相对路径从 current->cwd 开始
 *   follow_dotdot(dentry)    - 处理 ".." 分量，通过 d_parent 回溯
 *   lookup_one(parent, name) - 单级分量解析，调用 i_op->lookup
 */
