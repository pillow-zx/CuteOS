# cuteOS syscall 语义矩阵报告

本文是 cuteOS 当前 syscall 支持面的基线，记录
`include/kernel/syscall_table.h` 中 110 个 syscall 入口的语义成熟度。它不
是 Linux 完整兼容声明；入口存在只表示分发表安装了 handler，具体支持等级以
本文的 A/B/C/D 矩阵为准。

B/C/D 入口还在对应 `syscall/sys_*.c` handler 附近保留
`SYSCALL_SUPPORT(...)` 注释锚点，用来提示当前语义、unsupported errno 和后
续计划。修改实现语义时，应同步更新本文矩阵和源码锚点。

## 成熟度定义

| 等级 | 含义 | 后续策略 |
| --- | --- | --- |
| A | 常用语义可用；已有子系统承载真实行为 | 继续补边缘 flag 和压力测试 |
| B | 最小兼容或部分 Linux 语义；真实软件可用但有明显边界 | 优先加深语义 |
| C | 探测安全或兼容占位；存在 syscall 入口但语义很浅 | 明确文档，按真实软件需求推进 |
| D | 局部返回 `-ENOSYS` 或当前语义不应被依赖 | 不应宣传为支持 |

## 总览

| 域 | 数量 | 主要等级 | 主要风险 |
| --- | ---: | --- | --- |
| 文件描述符与 I/O | 20 | A/B | fcntl/ioctl/splice/fallocate flag 子集 |
| 路径、目录、挂载 | 18 | A/B/C | mount/umount 保持 C 级最小模型、rename/link 边界 |
| stat/statfs/statx | 5 | A/B | statx 扩展字段不置位、statfs 字段随 FS 声明 |
| poll/select/epoll | 6 | B | edge/oneshot、嵌套 epoll、信号掩码细节 |
| 进程、线程、等待 | 11 | A/B | clone flag、wait options、线程组细节 |
| signal | 7 | B | restart、默认动作、复杂 signal mask 细节 |
| 时间和 timer | 13 | B/C | REALTIME、clock_settime、POSIX timer 细节 |
| 内存管理 | 11 | B | file mmap、msync、mlock 语义深度 |
| futex/rseq/membarrier | 5 | B/C | futex opcode 子集、rseq flag、SMP 语义 |
| 身份、资源、杂项 | 14 | B/C/D | 权限模型、groups、syslog、随机源质量 |

## 文件描述符与 I/O

| Nr | syscall | 等级 | 当前语义 | 主要缺口 | 下一步 |
| ---: | --- | --- | --- | --- | --- |
| 23 | `dup` | A | fd 复制 | 无明显短期缺口 | 压测 fd 上限和 close-on-exec 交互 |
| 24 | `dup3` | A | 支持 `O_CLOEXEC` | 仅当前 fdtable 范围 | 加 dup3 同 fd errno 回归 |
| 25 | `fcntl` | B | 支持 `F_DUPFD/F_DUPFD_CLOEXEC/F_GETFD/F_SETFD/F_GETFL/F_SETFL`，`F_SETFL` 可修改 `O_APPEND/O_NONBLOCK`，见 cmd 支持表 | lock、lease、pipe size、owner 系列未实现但 errno 已固定 | 按表逐项替换为真实语义 |
| 29 | `ioctl` | B | 委托 `vfs_ioctl`；console 支持 termios、winsize、controlling tty、foreground pgid/session 查询设置 | pty、serial、line discipline 等 ioctl 未实现 | 按真实程序探测继续扩展 |
| 46 | `ftruncate64` | A | 通过 VFS truncate 文件 | 文件系统大文件边界有限 | 覆盖 sparse/truncate 扩展测试 |
| 47 | `fallocate` | B | 仅 `mode == 0`，限制最大文件大小 | punch hole/keep size 等 flag 不支持 | 固定 unsupported mode errno |
| 57 | `close` | A | fd close | 无明显短期缺口 | 加多线程/dup 交互测试 |
| 59 | `pipe2` | A | pipe 创建，支持 `O_CLOEXEC/O_NONBLOCK` | pipe size 调整未实现 | 补 `F_GETPIPE_SZ/F_SETPIPE_SZ` |
| 62 | `lseek` | A | VFS llseek | 特殊文件 seek 语义依赖 fops | 补设备/pipe ESPIPE 测试 |
| 63 | `read` | A | fd read + uaccess 分块，pipe 读端支持 `O_NONBLOCK` 空读 `-EAGAIN` | 非 pipe 文件的非阻塞语义有限 | 扩展更多文件类型的 nonblock 语义 |
| 64 | `write` | A | fd write + uaccess 分块，pipe 写端支持 `O_NONBLOCK` 满写 `-EAGAIN` | SIGPIPE/partial write 边界需继续测 | 扩展 pipe 写端测试 |
| 65 | `readv` | A | iovec 分块读取 | `IOV_MAX`、溢出细节 | 增加大 iov/partial 测试 |
| 66 | `writev` | A | iovec 分块写入 | 同 readv | 同 readv |
| 67 | `pread64` | A | offset 读，不动 file pos | 特殊文件 offset 语义 | 补 pipe/socket-like EBADF/ESPIPE |
| 68 | `pwrite64` | A | offset 写 | `O_APPEND` 语义需确认 | 增加 append + pwrite 测试 |
| 71 | `sendfile` | B | regular input 到 writable output，buffered copy | socket/pipe 等 Linux 场景缺失 | 明确只支持 file-to-file |
| 76 | `splice` | B | pipe/file 单边 splice，支持 hint flags | file-file、pipe-pipe、更多 flag 缺失 | 做 pipe/file 语义表 |
| 82 | `fsync` | A | VFS sync file | 元数据完整性取决于 FS | 加崩溃一致性非目标说明 |
| 83 | `fdatasync` | B | VFS 同步文件数据页，并调用 FS datasync hook 同步取回数据所需元数据；无 hook 时退化为完整 inode writeback | 崩溃写入顺序仍是 best-effort；各 FS 需要准确声明 datasync 元数据边界 | 按文件系统补强 ordering 语义 |

### `fcntl` cmd 支持表

`fcntl` 对 cuteOS 识别的 cmd 先检查 fd；fd 无效时返回 `-EBADF`。fd
有效但 cmd 不支持时返回表中的 unsupported errno。未识别 cmd 对有效 fd
返回 `-EINVAL`。

| cmd | 状态 | errno / 语义 |
| --- | --- | --- |
| `F_DUPFD` | supported | 复制到不小于 `arg` 的最低空闲 fd，清除 close-on-exec |
| `F_DUPFD_CLOEXEC` | supported | 同 `F_DUPFD`，并设置 close-on-exec |
| `F_GETFD` | supported | 返回 `FD_CLOEXEC` 或 0 |
| `F_SETFD` | supported | 只采用 `FD_CLOEXEC` 位，其它位忽略 |
| `F_GETFL` | supported | 返回 `O_ACCMODE/O_APPEND/O_NONBLOCK/O_DIRECTORY` 子集 |
| `F_SETFL` | supported | 只允许修改 `O_APPEND/O_NONBLOCK`；`O_DSYNC/FASYNC/O_DIRECT/O_NOATIME/O_SYNC` 返回 `-EINVAL` |
| `F_GETLK/F_SETLK/F_SETLKW/F_GETLK64/F_SETLK64/F_SETLKW64` | unsupported | `-EINVAL` |
| `F_OFD_GETLK/F_OFD_SETLK/F_OFD_SETLKW` | unsupported | `-EINVAL` |
| `F_SETOWN/F_GETOWN/F_SETSIG/F_GETSIG/F_SETOWN_EX/F_GETOWN_EX/F_GETOWNER_UIDS` | unsupported | `-EINVAL` |
| `F_SETLEASE/F_GETLEASE` | unsupported | `-EINVAL` |
| `F_NOTIFY` | unsupported | `-EINVAL` |
| `F_DUPFD_QUERY/F_CREATED_QUERY/F_CANCELLK` | unsupported | `-EINVAL` |
| `F_SETPIPE_SZ/F_GETPIPE_SZ` | unsupported | `-EINVAL` |
| `F_ADD_SEALS/F_GET_SEALS` | unsupported | `-EINVAL` |
| `F_GET_RW_HINT/F_SET_RW_HINT/F_GET_FILE_RW_HINT/F_SET_FILE_RW_HINT` | unsupported | `-EINVAL` |
| `F_GETDELEG/F_SETDELEG` | unsupported | `-EINVAL` |

## 路径、目录、挂载

| Nr | syscall | 等级 | 当前语义 | 主要缺口 | 下一步 |
| ---: | --- | --- | --- | --- | --- |
| 17 | `getcwd` | A | 从 `fs_struct` 和 VFS path 生成 cwd | mount namespace 不存在 | 保持 |
| 33 | `mknodat` | B | VFS mknod，应用 umask | 设备模型有限 | 明确支持 chr/blk/fifo 范围 |
| 34 | `mkdirat` | A | VFS mkdir + umask | 权限模型浅 | 补权限测试 |
| 35 | `unlinkat` | A | 支持 `AT_REMOVEDIR` | sticky bit/权限浅 | 补目录错误码 |
| 36 | `symlinkat` | B | VFS symlink | ext2 symlink 边界需压测 | 增加长 symlink 测试 |
| 37 | `linkat` | B | 支持 `AT_SYMLINK_FOLLOW` | 跨挂载、目录硬链接限制 | 明确 errno 表 |
| 39 | `umount2` | C | 单 namespace VFS unmount；拒绝所有非零 flag；root/busy mount 返回 `-EBUSY` | 无 lazy/force/no-follow、namespace 语义 | 保持探测安全最小模型 |
| 40 | `mount` | C | 单 namespace、显式 `ext2`、block-device source 到 directory target，`flags == 0` | 无 bind/remount/move/propagation/read-only、无 fs_context | 保持探测安全最小模型 |
| 48 | `faccessat` | B | 权限检查通过 VFS | real/effective id 差异浅 | 与 cred 模型一起加深 |
| 49 | `chdir` | A | path lookup 后切 cwd | mount namespace 不存在 | 保持 |
| 56 | `openat` | A | dirfd/path/flags/umask/VFS open | flag 组合仍需持续补 | 用 busybox/coreutils trace 补缺口 |
| 61 | `getdents64` | A | readdir 转 linux dirent64 | d_type 准确性依赖 FS | 增加多 chunk 目录测试 |
| 78 | `readlinkat` | A | nofollow 读取 symlink | `/proc/self/fd` 不存在 | 保持 |
| 88 | `utimensat` | B | 支持 NOW/OMIT、nofollow/empty path | 权限和 ctime 细节浅 | 加权限/ctime 测试 |
| 276 | `renameat2` | B | 支持 `RENAME_NOREPLACE` | exchange/whiteout 不支持 | 文档化 flag policy |
| 439 | `faccessat2` | B | 支持 `AT_EACCESS/EMPTY_PATH/NOFOLLOW` | cred 语义浅 | 和权限模型一起推进 |

### `mount` / `umount2` 最小模型

`mount`/`umount2` 当前保持 C 级：用于让真实软件探测到清晰边界，而不是宣
传完整 Linux 动态挂载。已支持模型是全局单 namespace 中把一个已存在块设备
节点挂到一个已存在目录，文件系统类型必须显式为 `ext2`，`data` 当前不解释，
`flags` 必须为 0。

| 场景 | errno / 语义 |
| --- | --- |
| `mount(blockdev, dir, "ext2", 0, NULL)` | supported；在目标目录覆盖处安装新的 ext2 root |
| unknown filesystem type | `-ENODEV` |
| source 不是 block device | `-ENOTBLK` |
| target 不是目录 | `-ENOTDIR` |
| duplicate mountpoint | `-EBUSY` |
| any nonzero `MS_*` flag | `-EINVAL` |
| `umount2(target, 0)` on non-root non-busy mount root | supported |
| root mount or active file/cwd/dirfd refs | `-EBUSY` |
| non-mount target | `-EINVAL` |
| any nonzero `MNT_*`/`UMOUNT_*` flag | `-EINVAL` |

## stat/statfs/statx

| Nr | syscall | 等级 | 当前语义 | 主要缺口 | 下一步 |
| ---: | --- | --- | --- | --- | --- |
| 43 | `statfs64` | B | VFS statfs；ext2 字段见下表 | 只有 ext2 声明完整字段；mount flags 固定 0 | 新 FS 按字段表声明 |
| 44 | `fstatfs64` | B | fd -> mount superblock statfs；语义同路径 statfs | 同 statfs64 | 同 statfs64 |
| 79 | `newfstatat` | A | 支持 empty path/nofollow | inode 属性完整度依赖 FS | 补 uid/gid/nsec 测试 |
| 80 | `fstat` | A | fd stat | 同 newfstatat | 保持 |
| 291 | `statx` | B | 从 VFS stat 转换真实 `STATX_BASIC_STATS`；扩展请求 probe-safe | btime、mount id、DIO alignment、attribute mask 缺失且不置位 | 有真实 backing state 后逐项增加 |

### `statx` mask 支持表

`statx` 接受 `AT_STATX_FORCE_SYNC`、`AT_STATX_DONT_SYNC` 和默认 sync 语义
作为本地文件系统 no-op hint。`mask` 的 `STATX__RESERVED` 位返回
`-EINVAL`；其它当前未支持的字段请求位作为探测接受，但结果中不会置位。
返回值可以包含调用者未显式请求但 VFS 已有的基础字段。

| result mask bit | 状态 | 字段语义 |
| --- | --- | --- |
| `STATX_BASIC_STATS` | supported | type/mode/nlink/uid/gid/atime/mtime/ctime/ino/size/blocks/blksize/dev/rdev |
| `STATX_BTIME` | unsupported | `stx_btime` 清零，result mask 不置位 |
| `STATX_MNT_ID` / `STATX_MNT_ID_UNIQUE` | unsupported | `stx_mnt_id` 清零，result mask 不置位 |
| `STATX_DIOALIGN` | unsupported | DIO alignment 字段清零，result mask 不置位 |
| `STATX_SUBVOL` | unsupported | `stx_subvol` 清零，result mask 不置位 |
| attributes / `stx_attributes_mask` | unsupported | 当前均为 0 |

### ext2 `statfs64` 字段表

| 字段 | ext2 当前语义 |
| --- | --- |
| `f_type` | `EXT2_SUPER_MAGIC` (`0xef53`) |
| `f_bsize` / `f_frsize` | 4 KiB ext2 block/page-cache block size |
| `f_blocks` | ext2 superblock total block count |
| `f_bfree` / `f_bavail` | group descriptor free block count；无配额/权限保留区差异，二者相同 |
| `f_files` / `f_ffree` | ext2 total/free inode count |
| `f_fsid` | ext2 UUID 派生的两个 32-bit 值；UUID 全零时回退到 device id |
| `f_namelen` | `EXT2_NAME_LEN` (`255`) |
| `f_flags` | 0；mount flags 未实现 |
| `f_spare` | 0 |

## poll/select/epoll

| Nr | syscall | 等级 | 当前语义 | 主要缺口 | 下一步 |
| ---: | --- | --- | --- | --- | --- |
| 20 | `epoll_create1` | B | 创建 eventpoll file，支持 cloexec | epoll fd 自身 readiness 浅 | 加嵌套/close 行为测试 |
| 21 | `epoll_ctl` | B | ADD/MOD/DEL，校验 event mask；`EPOLLET/EPOLLONESHOT` 返回 `-EINVAL` | edge/oneshot 未实现 | 按需求实现触发策略 |
| 22 | `epoll_pwait` | B | wait、临时 sigmask、timeout；可投递 signal 在无 ready event 时返回 `-EINTR` | event 触发模型简化 | 加嵌套/close 行为测试 |
| 72 | `pselect6` | B | fdset + timeout + 临时 sigmask；可投递 signal 在无 ready fd 时返回 `-EINTR` | 更复杂的 Linux signal race/restart 未实现 | 保持 interruption 回归测试 |
| 73 | `ppoll` | B | pollfd + timeout + 临时 sigmask；可投递 signal 在无 ready fd 时返回 `-EINTR` | 大 nfds 限 NR_OPEN | 文档化 NR_OPEN 限制 |

`ppoll`、`pselect6`、`epoll_pwait` 每轮先扫描 readiness。若 event/fd 已
ready，则返回 ready 数量并保留 signal pending；只有无 ready 结果时才以
`-EINTR` 返回。临时 signal mask 在 handler 完成后恢复原值。`-EINTR` 路径
不回写 `pollfd.revents`、fd sets 或 epoll events，也不修改 timeout 参数。

## 进程、线程、等待

| Nr | syscall | 等级 | 当前语义 | 主要缺口 | 下一步 |
| ---: | --- | --- | --- | --- | --- |
| 93 | `exit` | A | 当前任务退出 | 线程组细节依赖实现 | 保持 |
| 94 | `exit_group` | A | 线程组退出 | 多线程竞态待压测 | 加 clone-thread 测试 |
| 96 | `set_tid_addr` | A | 设置 clear_child_tid | futex wake 依赖退出路径 | 保持 |
| 124 | `sched_yield` | A | 主动让出 CPU | 单核 MLFQ 语义 | 保持 |
| 154 | `setpgid` | B | 设置同 session 内 process group，拒绝 session leader | exec-time EACCES、orphaned pgrp 规则未实现 | 和 shell job control 继续压测 |
| 155 | `getpgid` | B | 查询 pgid | 权限模型浅 | 保持并补跨进程测试 |
| 156 | `getsid` | B | 查询 sid，采用 Linux 不返回跨 session EPERM 的行为 | 权限模型浅 | 保持 |
| 157 | `setsid` | B | 创建新 session 和 process group | controlling tty 仅 single-console 模型 | 保持最小 job-control 语义 |
| 172 | `getpid` | A | 返回 tgid | 保持 |
| 173 | `getppid` | A | 返回 parent pid | orphan/adoption 已依赖 task | 保持 |
| 178 | `gettid` | A | 返回 task pid | 保持 |
| 220 | `clone` | B | 支持 fork-like 和线程子集，见 flag 支持表 | clone3/vfork/namespace 不支持 | 按真实线程库需求扩展 |
| 221 | `execve` | A | 通过 VFS 加载静态 ELF，复制 argv/envp，安装新 mm | 动态链接、解释器脚本、auxv 完整度有限 | 保持静态 ELF 主线，补 auxv/错误码测试 |
| 260 | `wait4` | B | 等待 pid `-1` 或正 pid，`options == 0`，返回 rusage | options/pgrp wait 不完整 | 扩展 `WNOHANG/WUNTRACED` 等 |

### `clone` flag 支持表

`clone` 当前覆盖 fork-like clone 和 pthread 所需线程子集。复杂 Linux
模型先固定为 `-EINVAL`，避免真实程序把探测成功误认为完整支持。

| flag / 组合 | 状态 | errno / 语义 |
| --- | --- | --- |
| exit signal `0` / `SIGCHLD` | supported | 非线程 clone 可用；其它非零 exit signal 返回 `-EINVAL` |
| `CLONE_VM` | supported | 共享 mm；必须提供 child stack，并且必须同时设置 `CLONE_SIGHAND` |
| `CLONE_FS` | supported | 共享 cwd/root/umask 状态 |
| `CLONE_FILES` | supported | 共享 fdtable |
| `CLONE_SIGHAND` | supported | 共享 handler 表；必须同时设置 `CLONE_VM` |
| `CLONE_THREAD` | supported | 加入调用者线程组；要求 `CLONE_VM | CLONE_SIGHAND` |
| `CLONE_PARENT_SETTID` | supported | commit 前把 child TID 写入 parent_tid；写失败返回 `-EFAULT` 并回滚 |
| `CLONE_CHILD_SETTID` | supported for threads | 线程 clone 写 child_tid；非线程 clone 返回 `-EINVAL` |
| `CLONE_CHILD_CLEARTID` | supported for threads | 线程退出时清零并 futex wake；非线程 clone 返回 `-EINVAL` |
| `CLONE_SETTLS` | supported for threads | 设置 child TLS；非线程 clone 返回 `-EINVAL` |
| `CLONE_DETACHED/CLONE_UNTRACED` | supported no-op | 无 ptrace 模型，作为兼容 hint 接受 |
| `CLONE_SYSVSEM` | supported no-op | 无 SysV semaphore undo 状态；为 musl-style pthread flag 组合接受 |
| `CLONE_PIDFD` | unsupported | `-EINVAL`；没有 pidfd 文件描述符模型 |
| `CLONE_VFORK` | unsupported | `-EINVAL`；没有 parent blocking / mm_release 语义 |
| `CLONE_PARENT` | unsupported | `-EINVAL`；不改变 parent 关系 |
| `CLONE_PTRACE` | unsupported | `-EINVAL`；没有 ptrace 模型 |
| namespace flags | unsupported | `CLONE_NEWTIME/NEWNS/NEWCGROUP/NEWUTS/NEWIPC/NEWUSER/NEWPID/NEWNET` 返回 `-EINVAL` |
| `CLONE_IO` | unsupported | `-EINVAL`；没有 io context |
| clone3-only flags | unsupported | `CLONE_CLEAR_SIGHAND/CLONE_INTO_CGROUP` 返回 `-EINVAL` |
| unknown flag bits | unsupported | `-EINVAL` |
| non-leader non-thread clone | unsupported | `-EINVAL` |

## signal

| Nr | syscall | 等级 | 当前语义 | 主要缺口 | 下一步 |
| ---: | --- | --- | --- | --- | --- |
| 129 | `kill` | B | 正 pid 投递 | pid 0/-1/process group 未支持 | 扩展 POSIX pid 语义 |
| 130 | `tkill` | B | tid 投递 | 权限模型浅 | 补 cred 检查 |
| 131 | `tgkill` | B | tgid+tid 投递 | 权限模型浅 | 同 tkill |
| 132 | `sigaltstack` | B | 注册/查询 altstack | SS_AUTODISARM 等未支持 | 明确 flag policy |
| 134 | `rt_sigaction` | B | handler/mask，支持 `SA_ONSTACK/NODEFER/RESETHAND`；保存 `SA_RESTART` | 无 syscall restart、无三参数 siginfo handler | 实现 restart frame 前保持固定策略 |
| 135 | `rt_sigprocmask` | B | 设/查 blocked mask | sigset size 固定 unsigned long | 保持 ABI 断言 |
| 139 | `rt_sigreturn` | B | 恢复用户上下文/mask，拒绝非用户 PC 和特权 `sstatus` | 无 syscall restart frame | 继续补架构状态边界测试 |

### `rt_sigaction` flag 支持表

| flag | 状态 | 当前语义 |
| --- | --- | --- |
| `SA_ONSTACK` | supported | altstack 可用且当前不在 altstack 时在其上构造 frame |
| `SA_NODEFER` | supported | 不自动屏蔽当前 signal；`sa_mask` 仍正常应用 |
| `SA_RESETHAND` | supported | 进入 handler 前将 disposition 重置为 `SIG_DFL` |
| `SA_RESTART` | accepted, inactive | 保存并可查询；当前不重启任何 syscall，等待仍返回 `-EINTR` |
| `SA_SIGINFO` | unsupported | 返回 `-EINVAL`；当前 handler interface 仅传 signal number |
| `SA_NOCLDSTOP` / `SA_NOCLDWAIT` | unsupported | 返回 `-EINVAL`；尚无对应 child-state 语义 |
| unknown bits | unsupported | 返回 `-EINVAL`，避免伪装支持 |

`SIG_DFL` 和 `SIG_IGN` disposition 可以保存已支持 flag；只有实际用户 handler
会消费 `SA_ONSTACK`、`SA_NODEFER`、`SA_RESETHAND` 行为。

## 时间和 timer

| Nr | syscall | 等级 | 当前语义 | 主要缺口 | 下一步 |
| ---: | --- | --- | --- | --- | --- |
| 101 | `nanosleep` | A | timer sleep；signal 打断返回 `-EINTR` 并写相对 remainder | 精度受 tick/mtime 影响 | 保持 |
| 102 | `getitimer` | B | 支持 itimer 状态查询 | 主要支持 REAL | 文档化 `ITIMER_REAL` 优先 |
| 103 | `setitimer` | B | 支持 `ITIMER_REAL` | virtual/prof 返回 EINVAL | 需要 CPU accounting 后再扩展 |
| 107 | `timer_create` | B | POSIX timer，支持 SIGEV_NONE/SIGNAL | SIGEV_THREAD、clock 细节缺失 | 补 sigevent 支持表 |
| 108 | `timer_gettime` | B | 查询 timer | 保持 |
| 109 | `timer_getoverrun` | B | overrun 计数 | 信号队列语义简化 | 跟 signal queue 一起推进 |
| 110 | `timer_settime` | B | 支持 relative/absolute | clock realtime 偏移缺失 | 补测试 |
| 111 | `timer_delete` | B | 删除 timer | 保持 |
| 112 | `clock_settime` | C | REALTIME 返回 `-EPERM`，其它 `-EINVAL` | 无 RTC/wall-clock offset | 加 RTC/offset 前保持 |
| 113 | `clock_gettime` | B | REALTIME/MONOTONIC/BOOTTIME 基于 mtime | REALTIME 不是真实 wall clock | 文档化 |
| 114 | `clock_getres` | A | 返回 mtime 分辨率 | 保持 |
| 115 | `clock_nanosleep` | B | relative/absolute sleep；relative `-EINTR` 写 remainder，absolute 不修改 remainder | clock 差异浅 | 与 clock_gettime 同步 |

`SA_RESTART` 当前不改变 sleep 行为。`nanosleep` 和相对
`clock_nanosleep` 被 signal 打断时返回 `-EINTR`，remainder 满足
`0 < remainder <= request`。绝对 `clock_nanosleep(TIMER_ABSTIME)` 同样返回
`-EINTR`，但不写 remainder。
| 153 | `times` | B | 当前/系统 tick，child time 部分 | 更完整 cputime 统计 | 补 child cputime 测试 |
| 169 | `gettimeofday` | B | 基于启动后 mtime，timezone UTC | 无 RTC | 等平台 RTC |

## 内存管理

| Nr | syscall | 等级 | 当前语义 | 主要缺口 | 下一步 |
| ---: | --- | --- | --- | --- | --- |
| 214 | `brk` | A | 堆增长/查询，不缩小 | Linux shrink 行为不同 | 保持或明确兼容差异 |
| 215 | `munmap` | A | VMA 拆分和页释放 | 并发不相关 | 保持 |
| 216 | `mremap` | B | 支持 shrink/grow、`MREMAP_MAYMOVE`、`MREMAP_FIXED`，见 flag 支持表 | `MREMAP_DONTUNMAP` 不支持 | 需要源区保留契约后再扩展 |
| 222 | `mmap` | B | 匿名和 regular file-backed `MAP_PRIVATE/MAP_SHARED`，支持 `MAP_SHARED_VALIDATE`、`MAP_FIXED_NOREPLACE`、`MAP_POPULATE`，见 flag 支持表 | 无 huge page、locked/pinned 语义；越过文件大小的 Linux `SIGBUS` 语义未实现 | 按真实程序需求补 pin/更深 file fault |
| 226 | `mprotect` | B | VMA 拆分和 resident PTE 权限更新，覆盖跨 VMA 范围 | W^X/exec cache 细节 | 保持跨 VMA/slot 压测 |
| 227 | `msync` | B | 验证映射范围；resident shared file 页标脏；`MS_SYNC` 通过 VFS/page cache 写回 | `MS_INVALIDATE` 仅 no-op；非 range-limited inode writeback | page cache 有范围写回后收窄同步范围 |
| 228 | `mlock` | C | 当前更偏验证/占位语义 | 无完整 resident pin/limit | 不宣传完整支持 |
| 229 | `munlock` | C | 同 mlock | 同 mlock | 同 mlock |
| 232 | `mincore` | B | 查询匿名和 file-backed 映射的 resident PTE bit | 不报告仅存在于 page cache 的文件页 | 保持为 resident 用户映射查询 |
| 233 | `madvise` | B | 建立 advice 表；`DONTNEED` 释放匿名、private file、shared file resident 页 | `FREE/WILLNEED` 等仍是 no-op hint；`REMOVE` 不支持 | 有 backing-store 语义后再扩展 destructive advice |

### `mmap` flag 支持表

`mmap` 先按 Linux `MAP_TYPE` 解析映射类型。`MAP_SHARED_VALIDATE` 的普通
映射语义与 `MAP_SHARED` 相同；携带 cuteOS 不支持的扩展 flag 时返回
`-EOPNOTSUPP`。

| flag / type | 状态 | errno / 语义 |
| --- | --- | --- |
| `MAP_PRIVATE` | supported | 匿名映射分配私有零页；file-backed 映射按需从 page cache 拷贝私有页，修改不写回 |
| `MAP_SHARED` | supported | regular file 页直接映射 page cache；resident writable 页在 `msync`/`munmap` 时标脏并可写回 |
| `MAP_FIXED` | supported | 使用精确地址，先 unmap 目标范围 |
| `MAP_FIXED_NOREPLACE` | supported | 使用精确地址但不覆盖已有 VMA；冲突返回 `-EEXIST` |
| `MAP_ANONYMOUS` | supported | 不使用 fd；仍要求 `MAP_PRIVATE` 或 `MAP_SHARED` 类型 |
| `MAP_DENYWRITE/MAP_EXECUTABLE/MAP_NORESERVE/MAP_STACK` | supported no-op | 兼容真实程序常见 hint，不改变布局或分配策略 |
| `MAP_SHARED_VALIDATE` | supported | 按 shared mapping 创建；不支持的 validate 扩展 flag 返回 `-EOPNOTSUPP` |
| `MAP_POPULATE` | supported | 建立 VMA 后尽力 fault-in 普通匿名/file-backed 页；prefault 失败不撤销成功映射 |
| `MAP_GROWSDOWN/MAP_LOCKED/MAP_NONBLOCK/MAP_HUGETLB/MAP_SYNC` | unsupported | 普通 mapping 返回 `-EINVAL`；`MAP_SHARED_VALIDATE` 下返回 `-EOPNOTSUPP` |
| unknown flag bits | unsupported | 普通 mapping 返回 `-EINVAL`；`MAP_SHARED_VALIDATE` 下返回 `-EOPNOTSUPP` |

### `msync` flag 支持表

`msync` 要求地址页对齐，范围必须已映射；未映射页返回 `-ENOMEM`。

| flag | 状态 | errno / 语义 |
| --- | --- | --- |
| `0` / `MS_ASYNC` | supported | 验证范围；对 resident shared file 页记录 dirty 状态但不等待写回 |
| `MS_SYNC` | supported | 记录 dirty 状态并通过 VFS/page cache 同步对应 file |
| `MS_INVALIDATE` | supported no-op | 接受但不主动丢弃其它映射或 page cache |
| `MS_ASYNC | MS_SYNC` | unsupported | `-EINVAL` |
| unknown flag bits | unsupported | `-EINVAL` |

### `madvise` advice 支持表

| advice | 状态 | errno / 语义 |
| --- | --- | --- |
| `MADV_NORMAL/MADV_RANDOM/MADV_SEQUENTIAL` | supported no-op | 验证范围后返回成功 |
| `MADV_WILLNEED` | supported no-op | 不 prefault，只固定 probe-safe 成功行为 |
| `MADV_FREE` | supported no-op | 不建立 lazy-free 状态 |
| `MADV_DONTNEED` | supported | 丢弃 resident 页；匿名重新 fault 得零页，private file 重新从文件读取，shared file 保留 page-cache 数据并丢弃 PTE |
| `MADV_REMOVE` | unsupported | `-EINVAL` |
| unknown advice | unsupported | `-EINVAL` |

### `mremap` flag 支持表

| flag / 组合 | 状态 | errno / 语义 |
| --- | --- | --- |
| `0` | supported | 原地 shrink/grow；无法原地 grow 时返回 `-ENOMEM` |
| `MREMAP_MAYMOVE` | supported | 原地失败时移动到内核选择的新地址 |
| `MREMAP_MAYMOVE | MREMAP_FIXED` | supported | 移动到页对齐的指定地址，替换目标范围 |
| `MREMAP_FIXED` without `MREMAP_MAYMOVE` | unsupported | `-EINVAL` |
| `MREMAP_DONTUNMAP` | unsupported | `-EINVAL` |
| unknown flag bits | unsupported | `-EINVAL` |

## futex/rseq/membarrier

| Nr | syscall | 等级 | 当前语义 | 主要缺口 | 下一步 |
| ---: | --- | --- | --- | --- | --- |
| 98 | `futex` | B | 支持 WAIT/WAKE、WAIT_BITSET/WAKE_BITSET、PRIVATE aliases、wait op realtime option 和 robust exit wake，见 op 支持表 | requeue、PI、shared inode key 缺失 | 按 pthread 需求扩展 |
| 99 | `set_robust_list` | B | 登记 robust list | 仅 exit-time 遍历 | 保持并压测非法链 |
| 100 | `get_robust_list` | B | 查询 robust list | 权限模型浅 | 补跨线程权限 |
| 283 | `membarrier` | B | 单核 private/global/sync_core/rseq 兼容，见 cmd 支持表 | 无 SMP IPI/runqueue 语义 | SMP 前保持单核标注 |
| 293 | `rseq` | B | 单核注册/注销/abort-on-preempt/signal，见 flag 支持表 | SMP migrate、完整 mm_cid 不支持 | 跟 SMP 一起扩展 |

### `futex` op 支持表

futex key 当前是 `(mm, uaddr)`；`FUTEX_PRIVATE_FLAG` 明确支持并覆盖
pthread 路径，跨进程 shared futex 的 inode/page-cache key 尚未实现。

| op / flag | 状态 | errno / 语义 |
| --- | --- | --- |
| `FUTEX_WAIT` / `FUTEX_WAIT_PRIVATE` | supported | 值不匹配返回 `-EAGAIN`；relative timeout 可返回 `-ETIMEDOUT`；signal 打断返回 `-EINTR` |
| `FUTEX_WAKE` / `FUTEX_WAKE_PRIVATE` | supported | 返回唤醒 waiter 数；`nr <= 0` 返回 0 |
| `FUTEX_PRIVATE_FLAG` | supported | 作为 pthread fast path 固定接受；当前 private/shared 都使用 mm-local key |
| `FUTEX_CLOCK_REALTIME` | supported for wait ops | `FUTEX_WAIT` / `FUTEX_WAIT_BITSET` 接受；当前 `CLOCK_REALTIME` 与 mtime 同源，不承诺真实墙钟 |
| `FUTEX_WAIT_BITSET/WAKE_BITSET` | supported | `val3 == 0` 返回 `-EINVAL`；WAIT_BITSET timeout 为 absolute；WAKE_BITSET 只唤醒 bitset 相交 waiter |
| `FUTEX_REQUEUE/CMP_REQUEUE/WAKE_OP` | unsupported | `-ENOSYS` |
| PI futex ops | unsupported | `FUTEX_LOCK_PI/UNLOCK_PI/TRYLOCK_PI/LOCK_PI2` 返回 `-ENOSYS` |
| requeue-PI ops | unsupported | `FUTEX_WAIT_REQUEUE_PI/CMP_REQUEUE_PI` 返回 `-ENOSYS` |
| unknown op bits | unsupported | `-ENOSYS` |

### `rseq` flag 支持表

| flag / field | 状态 | errno / 语义 |
| --- | --- | --- |
| syscall flags `0` | supported | 注册当前线程 rseq area；写入 CPU/node/mm_cid 单核值 0 |
| `RSEQ_FLAG_UNREGISTER` | supported | 注销当前线程；area/len/sig 不匹配返回 `-EINVAL` 或 `-EPERM` |
| unknown syscall flags | unsupported | `-EINVAL` |
| duplicate matching registration | supported error | `-EBUSY` |
| duplicate with wrong signature | supported error | `-EPERM` |
| `struct rseq.flags` | ignored | kernel 写 0；当前不解释 userspace 后续写入 |
| `rseq_cs.version == 0` | supported | 其它版本在用户返回处理 active CS 时触发 `SIGSEGV` 退出 |
| `RSEQ_CS_FLAG_NO_RESTART_ON_PREEMPT` | supported | timer preempt 后不跳转 abort handler |
| `RSEQ_CS_FLAG_NO_RESTART_ON_SIGNAL` | supported | signal delivery 不跳转 abort handler |
| `RSEQ_CS_FLAG_NO_RESTART_ON_MIGRATE` | supported no-op | 当前单核无 migrate 事件 |
| unknown `rseq_cs.flags` | unsupported | active CS 遇到未知 bit 时触发 `SIGSEGV` 退出 |

### `membarrier` cmd 支持表

membarrier 当前只承诺单核兼容语义：本地 fence、registration bitmask 和
CPU0 参数校验。没有 SMP IPI、remote runqueue 扫描或跨 hart expedited
保证；SMP 前不应宣传完整 Linux expedited 行为。

| cmd / flag | 状态 | errno / 语义 |
| --- | --- | --- |
| `MEMBARRIER_CMD_QUERY` | supported | 返回 cuteOS 当前支持的 cmd bitmask |
| `MEMBARRIER_CMD_GLOBAL` | supported single-core | 执行本地 full memory barrier |
| `MEMBARRIER_CMD_GLOBAL_EXPEDITED` | supported single-core | 执行本地 full memory barrier；不要求调用者注册 |
| `REGISTER_*` cmds | supported | 记录到当前 mm registration bitmask 并执行本地 barrier |
| `MEMBARRIER_CMD_PRIVATE_EXPEDITED` | supported single-core | 未注册返回 `-EPERM`；注册后本地 full barrier |
| `MEMBARRIER_CMD_PRIVATE_EXPEDITED_SYNC_CORE` | supported single-core | 未注册返回 `-EPERM`；注册后执行本地 icache/core sync hook |
| `MEMBARRIER_CMD_PRIVATE_EXPEDITED_RSEQ` | supported single-core | 未注册返回 `-EPERM`；注册后本地 barrier |
| `MEMBARRIER_CMD_FLAG_CPU` | supported for RSEQ only | 只接受 `cpu_id == 0`；其它 cmd 携带 flag 返回 `-EINVAL` |
| `MEMBARRIER_CMD_GET_REGISTRATIONS` | supported | 返回当前 mm registration bitmask；无 mm 返回 0 |
| unknown cmd / bad flags / bad CPU | unsupported | `-EINVAL` |

## 身份、资源、杂项

| Nr | syscall | 等级 | 当前语义 | 主要缺口 | 下一步 |
| ---: | --- | --- | --- | --- | --- |
| 116 | `syslog` | D | 仅 `SIZE_BUFFER`，其它返回 `-ENOSYS` | 无 printk ring buffer read/clear | 实现 ring buffer 或继续标 D |
| 122 | `sched_setaffinity` | C | 单核 mask 检查，接受 CPU0 | 不存储 per-task mask | SMP 前保持 C |
| 123 | `sched_getaffinity` | C | 固定返回 CPU0 mask | 同上 | SMP 前保持 C |
| 144 | `setgid` | B | root 或自身 gid 改写 | cred/capability 模型浅 | 建立 cred 模型 |
| 146 | `setuid` | B | root 或自身 uid 改写 | saved uid/euid 不完整 | 同 cred |
| 158 | `getgroups` | C | 固定单组 gid 0 | supplementary groups 缺失 | 和 cred 一起做 |
| 159 | `setgroups` | C | size 0 或单组 0 成功，其它 `-EPERM` | 无 groups 存储 | 和 cred 一起做 |
| 160 | `uname` | A | 固定 cuteOS/riscv64 信息 | 保持 |
| 165 | `getrusage` | B | self/children cputime 基础字段 | 许多资源字段置零 | 补内存/IO 统计或文档化 |
| 166 | `umask` | A | fs_struct umask | 保持 |
| 174 | `getuid` | A | 返回 uid | cred 模型浅但查询可用 | 保持 |
| 175 | `geteuid` | A | 当前等同 uid | euid 未分离 | cred 模型扩展 |
| 176 | `getgid` | A | 返回 gid | 保持 |
| 177 | `getegid` | A | 当前等同 gid | egid 未分离 | cred 模型扩展 |
| 179 | `sysinfo` | B | uptime、内存、进程数 | load、swap 等置零 | 文档化字段 |
| 261 | `prlimit64` | B | 基础 rlimit get/set | capability、资源强制执行有限 | 先落实 NOFILE/AS |
| 278 | `getrandom` | C | xorshift/mtime seed，flag 验证 | 非密码安全随机源 | 标为 weak random 或接入 entropy |

## 优先加深清单

1. `fcntl/ioctl/pipe2/openat`：真实程序探测频率高，先补 flag/errno 表。
2. `mmap/mprotect/msync/madvise/mincore`：决定动态运行库、数据库、小型语言运行时能否稳定。
3. `clone/futex/rseq/membarrier`：决定线程库和 libc 是否能可靠。
4. `signal/sigreturn/sigaction`：决定异步行为和 sleep/poll 中断语义。
5. `poll/epoll/pselect/ppoll`：决定事件循环程序的可用性。
6. `statx/statfs/mount`：决定现代工具链和 coreutils 探测路径。
