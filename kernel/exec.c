/*
 * kernel/exec.c - 程序执行（ELF 加载）
 *
 * 功能：
 *   实现 sys_execve(path, argv, envp) 系统调用。将 ELF 可执行文件
 *   加载到当前进程的地址空间，替换原有的内存映射和进程映像，
 *   并准备首次返回用户态执行新程序。
 *
 *   execve 执行流程：
 *     1. 通过 current->tf 获取当前进程的 trap_frame 指针。
 *     2. 读取 ELF 文件头，校验 ELF 魔数（\x7fELF）。
 *     3. 遍历程序头（Phdr），仅处理 PT_LOAD 类型的段，
 *        为每个 LOAD 段分配物理页并建立映射。
 *     4. 分配新的页全局目录（pgd），复制内核映射（pgd 高 256 项），
 *        映射 trampoline 页到用户页表中。
 *     5. 分配 4MB 用户栈。
 *     6. 在用户栈顶布置 argc 和 argv 指针数组。
 *     7. 设置 trap_frame 使返回用户态时：
 *          - sepc = ELF 入口点（e_entry）
 *          - sp = 用户栈顶
 *          - SPP = 0（确保返回 U-mode）
 *
 * 主要函数：
 *   sys_execve(path, argv, envp) - execve 系统调用入口。
 *   elf_load(binary, mm)         - 解析 ELF 文件头和程序头，
 *                    校验 ELF 魔数，仅处理 PT_LOAD 段，
 *                    为每个段分配物理页并建立用户态映射。
 *   setup_user_stack(mm, argv)   - 分配 4MB 用户栈，
 *                    在栈顶布置 argc + argv 指针数组。
 *   flush_old_exec(mm)           - 释放旧进程的全部用户地址空间
 *                    （VMA、物理页、旧 pgd），为新程序映像做准备。
 */
