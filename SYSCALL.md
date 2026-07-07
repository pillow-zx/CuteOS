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
| 路径、目录、挂载 | 18 | A/B/C | mount/umount 动态语义、rename/link 边界 |
| stat/statfs/statx | 5 | A/B | statx mask 和属性语义较浅 |
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
| 25 | `fcntl` | B | 支持 `F_DUPFD/F_DUPFD_CLOEXEC/F_GETFD/F_SETFD/F_GETFL/F_SETFL` | lock、lease、pipe size、owner 系列未实现 | 建立 fcntl cmd 支持表 |
| 29 | `ioctl` | B | 委托 `vfs_ioctl` | 设备 ioctl 覆盖有限 | 先扩展 tty/console 常用 ioctl |
| 46 | `ftruncate64` | A | 通过 VFS truncate 文件 | 文件系统大文件边界有限 | 覆盖 sparse/truncate 扩展测试 |
| 47 | `fallocate` | B | 仅 `mode == 0`，限制最大文件大小 | punch hole/keep size 等 flag 不支持 | 固定 unsupported mode errno |
| 57 | `close` | A | fd close | 无明显短期缺口 | 加多线程/dup 交互测试 |
| 59 | `pipe2` | A | pipe 创建，支持 `O_CLOEXEC` | `O_NONBLOCK` 未列为支持 | 决定是否支持 pipe nonblock |
| 62 | `lseek` | A | VFS llseek | 特殊文件 seek 语义依赖 fops | 补设备/pipe ESPIPE 测试 |
| 63 | `read` | A | fd read + uaccess 分块 | 非阻塞语义有限 | 跟随 `O_NONBLOCK` 计划 |
| 64 | `write` | A | fd write + uaccess 分块 | SIGPIPE/partial write 边界需继续测 | 扩展 pipe 写端测试 |
| 65 | `readv` | A | iovec 分块读取 | `IOV_MAX`、溢出细节 | 增加大 iov/partial 测试 |
| 66 | `writev` | A | iovec 分块写入 | 同 readv | 同 readv |
| 67 | `pread64` | A | offset 读，不动 file pos | 特殊文件 offset 语义 | 补 pipe/socket-like EBADF/ESPIPE |
| 68 | `pwrite64` | A | offset 写 | `O_APPEND` 语义需确认 | 增加 append + pwrite 测试 |
| 71 | `sendfile` | B | regular input 到 writable output，buffered copy | socket/pipe 等 Linux 场景缺失 | 明确只支持 file-to-file |
| 76 | `splice` | B | pipe/file 单边 splice，支持 hint flags | file-file、pipe-pipe、更多 flag 缺失 | 做 pipe/file 语义表 |
| 82 | `fsync` | A | VFS sync file | 元数据完整性取决于 FS | 加崩溃一致性非目标说明 |
| 83 | `fdatasync` | B | 当前等同 `fsync` | data-only 区分未实现 | 文档标注 intentional simplification |

## 路径、目录、挂载

| Nr | syscall | 等级 | 当前语义 | 主要缺口 | 下一步 |
| ---: | --- | --- | --- | --- | --- |
| 17 | `getcwd` | A | 从 `fs_struct` 和 VFS path 生成 cwd | mount namespace 不存在 | 保持 |
| 33 | `mknodat` | B | VFS mknod，应用 umask | 设备模型有限 | 明确支持 chr/blk/fifo 范围 |
| 34 | `mkdirat` | A | VFS mkdir + umask | 权限模型浅 | 补权限测试 |
| 35 | `unlinkat` | A | 支持 `AT_REMOVEDIR` | sticky bit/权限浅 | 补目录错误码 |
| 36 | `symlinkat` | B | VFS symlink | ext2 symlink 边界需压测 | 增加长 symlink 测试 |
| 37 | `linkat` | B | 支持 `AT_SYMLINK_FOLLOW` | 跨挂载、目录硬链接限制 | 明确 errno 表 |
| 39 | `umount2` | C | 存在 VFS 入口 | 动态 mount 模型未成熟 | 先定义 mount lifecycle |
| 40 | `mount` | C | 存在 VFS 入口 | fs_context、namespace、flag 语义浅 | 建立最小 mount 设计 |
| 48 | `faccessat` | B | 权限检查通过 VFS | real/effective id 差异浅 | 与 cred 模型一起加深 |
| 49 | `chdir` | A | path lookup 后切 cwd | mount namespace 不存在 | 保持 |
| 56 | `openat` | A | dirfd/path/flags/umask/VFS open | flag 组合仍需持续补 | 用 busybox/coreutils trace 补缺口 |
| 61 | `getdents64` | A | readdir 转 linux dirent64 | d_type 准确性依赖 FS | 增加多 chunk 目录测试 |
| 78 | `readlinkat` | A | nofollow 读取 symlink | `/proc/self/fd` 不存在 | 保持 |
| 88 | `utimensat` | B | 支持 NOW/OMIT、nofollow/empty path | 权限和 ctime 细节浅 | 加权限/ctime 测试 |
| 276 | `renameat2` | B | 支持 `RENAME_NOREPLACE` | exchange/whiteout 不支持 | 文档化 flag policy |
| 439 | `faccessat2` | B | 支持 `AT_EACCESS/EMPTY_PATH/NOFOLLOW` | cred 语义浅 | 和权限模型一起推进 |

## stat/statfs/statx

| Nr | syscall | 等级 | 当前语义 | 主要缺口 | 下一步 |
| ---: | --- | --- | --- | --- | --- |
| 43 | `statfs64` | B | VFS statfs | 多 FS/mount 信息有限 | 补 ext2 字段完整性 |
| 44 | `fstatfs64` | B | fd -> mount superblock statfs | 同 statfs64 | 同 statfs64 |
| 79 | `newfstatat` | A | 支持 empty path/nofollow | inode 属性完整度依赖 FS | 补 uid/gid/nsec 测试 |
| 80 | `fstat` | A | fd stat | 同 newfstatat | 保持 |
| 291 | `statx` | B | 从 `stat` 转 `STATX_BASIC_STATS` | btime、mount id、属性 mask 缺失 | 标注 mask 支持范围 |

## poll/select/epoll

| Nr | syscall | 等级 | 当前语义 | 主要缺口 | 下一步 |
| ---: | --- | --- | --- | --- | --- |
| 20 | `epoll_create1` | B | 创建 eventpoll file，支持 cloexec | epoll fd 自身 readiness 浅 | 加嵌套/close 行为测试 |
| 21 | `epoll_ctl` | B | ADD/MOD/DEL，校验 event mask | `EPOLLET/ONESHOT` 接受但 scan 不支持 | 明确拒绝或实现 |
| 22 | `epoll_pwait` | B | wait、sigmask、timeout | event 触发模型简化 | 优先修 edge/oneshot 策略 |
| 72 | `pselect6` | B | fdset + timeout + sigmask | 信号 race 语义简化 | 增加 signal interruption 测试 |
| 73 | `ppoll` | B | pollfd + timeout + sigmask | 大 nfds 限 NR_OPEN | 文档化 NR_OPEN 限制 |

## 进程、线程、等待

| Nr | syscall | 等级 | 当前语义 | 主要缺口 | 下一步 |
| ---: | --- | --- | --- | --- | --- |
| 93 | `exit` | A | 当前任务退出 | 线程组细节依赖实现 | 保持 |
| 94 | `exit_group` | A | 线程组退出 | 多线程竞态待压测 | 加 clone-thread 测试 |
| 96 | `set_tid_addr` | A | 设置 clear_child_tid | futex wake 依赖退出路径 | 保持 |
| 124 | `sched_yield` | A | 主动让出 CPU | 单核 MLFQ 语义 | 保持 |
| 154 | `setpgid` | B | 基础 process group | session/job control 不完整 | 为 shell job control 建模 |
| 155 | `getpgid` | B | 查询 pgid | 同 setpgid | 同 setpgid |
| 172 | `getpid` | A | 返回 tgid | 保持 |
| 173 | `getppid` | A | 返回 parent pid | orphan/adoption 已依赖 task | 保持 |
| 178 | `gettid` | A | 返回 task pid | 保持 |
| 220 | `clone` | B | 支持 fork-like 和线程子集，拒绝 namespace/vfork/io | clone3/vfork/namespace 不支持 | 完善 clone flag 表 |
| 221 | `execve` | A | 通过 VFS 加载静态 ELF，复制 argv/envp，安装新 mm | 动态链接、解释器脚本、auxv 完整度有限 | 保持静态 ELF 主线，补 auxv/错误码测试 |
| 260 | `wait4` | B | 等待 pid `-1` 或正 pid，`options == 0`，返回 rusage | options/pgrp wait 不完整 | 扩展 `WNOHANG/WUNTRACED` 等 |

## signal

| Nr | syscall | 等级 | 当前语义 | 主要缺口 | 下一步 |
| ---: | --- | --- | --- | --- | --- |
| 129 | `kill` | B | 正 pid 投递 | pid 0/-1/process group 未支持 | 扩展 POSIX pid 语义 |
| 130 | `tkill` | B | tid 投递 | 权限模型浅 | 补 cred 检查 |
| 131 | `tgkill` | B | tgid+tid 投递 | 权限模型浅 | 同 tkill |
| 132 | `sigaltstack` | B | 注册/查询 altstack | SS_AUTODISARM 等未支持 | 明确 flag policy |
| 134 | `rt_sigaction` | B | handler/mask 基础语义 | SA_RESTART/SA_SIGINFO 等细节 | 建立 signal flag 矩阵 |
| 135 | `rt_sigprocmask` | B | 设/查 blocked mask | sigset size 固定 unsigned long | 保持 ABI 断言 |
| 139 | `rt_sigreturn` | B | 从 signal frame 恢复上下文 | frame 安全和 restart 细节 | 继续补非法 frame 测试 |

## 时间和 timer

| Nr | syscall | 等级 | 当前语义 | 主要缺口 | 下一步 |
| ---: | --- | --- | --- | --- | --- |
| 101 | `nanosleep` | A | timer sleep，支持 EINTR remainder | 精度受 tick/mtime 影响 | 保持 |
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
| 115 | `clock_nanosleep` | B | 支持 relative/absolute sleep | clock 差异浅 | 与 clock_gettime 同步 |
| 153 | `times` | B | 当前/系统 tick，child time 部分 | 更完整 cputime 统计 | 补 child cputime 测试 |
| 169 | `gettimeofday` | B | 基于启动后 mtime，timezone UTC | 无 RTC | 等平台 RTC |

## 内存管理

| Nr | syscall | 等级 | 当前语义 | 主要缺口 | 下一步 |
| ---: | --- | --- | --- | --- | --- |
| 214 | `brk` | A | 堆增长/查询，不缩小 | Linux shrink 行为不同 | 保持或明确兼容差异 |
| 215 | `munmap` | A | VMA 拆分和页释放 | 并发不相关 | 保持 |
| 216 | `mremap` | B | 基础 remap | 复杂移动/固定地址语义需确认 | 建立 mremap flag 表 |
| 222 | `mmap` | B | 匿名和 file-backed mmap 入口 | shared/writeback、权限细节 | 优先加深 file mmap |
| 226 | `mprotect` | B | VMA 拆分和 PTE 权限更新 | W^X/exec cache 细节 | 补跨 VMA 测试 |
| 227 | `msync` | B | 验证并委托 MM | shared file mapping 深度有限 | 与 mmap writeback 一起推进 |
| 228 | `mlock` | C | 当前更偏验证/占位语义 | 无完整 resident pin/limit | 不宣传完整支持 |
| 229 | `munlock` | C | 同 mlock | 同 mlock | 同 mlock |
| 232 | `mincore` | B | 查询 resident bit | file cache residency 语义有限 | 补 file-backed 测试 |
| 233 | `madvise` | B | DONTNEED 释放匿名 resident，其他 advice 验证 | WILLNEED/FREE 等浅语义 | 建立 advice 支持表 |

## futex/rseq/membarrier

| Nr | syscall | 等级 | 当前语义 | 主要缺口 | 下一步 |
| ---: | --- | --- | --- | --- | --- |
| 98 | `futex` | B | 支持 WAIT/WAKE 和 robust exit wake | requeue、pi、wait_bitset 等返回 `-ENOSYS` | 按 pthread 需求扩展 |
| 99 | `set_robust_list` | B | 登记 robust list | 仅 exit-time 遍历 | 保持并压测非法链 |
| 100 | `get_robust_list` | B | 查询 robust list | 权限模型浅 | 补跨线程权限 |
| 283 | `membarrier` | B | 单核 private/global/sync_core/rseq 兼容 | 无 SMP IPI/runqueue 语义 | SMP 前标单核兼容 |
| 293 | `rseq` | B | 单核注册/注销/abort-on-preempt/signal | flags、SMP migrate、完整 mm_cid 不支持 | 补 rseq flag 策略 |

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
