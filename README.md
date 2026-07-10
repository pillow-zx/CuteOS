# cuteOS

cuteOS 是一个面向教学和自研实验的 RISC-V 64 Unix-like 内核。它运行在
QEMU `virt` 平台上，经 OpenSBI 启动，使用 Sv39 页表、高半区内核映射和
Linux riscv64 用户态 ABI。

当前代码的目标不是做一个只会打印日志的 boot demo，而是用小而清晰的内核结
构承载真实静态 riscv64 ELF 程序所需的关键 Linux ABI 行为。项目已经打通从
OpenSBI、内核初始化、virtio-blk 根块设备、VFS 自动探测并挂载根文件系统，
到 PID 1 `/bin/init` 和交互式 `/bin/sh` 的主链路。

## 项目定位

cuteOS 的核心取舍是：优先保证 ABI 行为、边界清晰和可调试性，再逐步扩大
Linux 语义覆盖面。

- 架构：RISC-V 64，`rv64gc`，Sv39。
- 平台：QEMU `virt`，OpenSBI，S-mode 内核。
- CPU：当前只启动 hart 0，`NR_CPUS` 和 CPU-local 结构为未来多核保留。
- 地址空间：高半区内核，用户页表复制内核高半区映射。
- 用户态：静态 ELF64 RISC-V 程序，Linux riscv64 syscall ABI。
- 根文件系统：构建时生成 ext2 镜像，启动时通过 VFS 探测 virtio-blk 根块设
  备并挂载为 `/`。
- 调度：单核 4 级 MLFQ，内核非抢占，用户态在 timer trap 返回点抢占。
- I/O：UART 和 virtio-blk 以轮询为主。
- 错误约定：内核内部和 syscall 返回统一使用 Linux 数值的负 errno。

项目面向已经接触过 xv6、rCore 或类似教学内核，并希望继续理解真实 ABI、文
件系统、用户态程序和内核子系统边界如何落地的读者。

## 当前运行体验

启动后会进入一个小型 shell：

```text
CuteOS shell
/$ ls
/$ echo hello > /tmp.txt
/$ cat /tmp.txt
hello
/$ mkdir -p /a/b
/$ cp /tmp.txt /a/b/copy.txt
/$ ls -l /a/b
/$ cat /tmp.txt | cat
hello
```

当前可以运行仓库内置的用户态命令和测试程序，支持基础文件操作、进程创建、
线程子集、管道、重定向、信号、匿名和文件映射、poll/epoll 兼容入口、rseq
单核兼容路径，以及 ext2 读写。

## 支持范围

`include/kernel/syscall_table.h` 当前安装了 110 个 Linux riscv64 syscall 入
口。入口存在不等于完整 Linux 兼容。支持面以
[SYSCALL.md](SYSCALL.md) 的 A/B/C/D 语义矩阵为准：

| 等级 | 含义 |
| --- | --- |
| A | 常用语义可用，已有真实子系统承载行为 |
| B | 最小兼容或部分 Linux 语义，真实软件可用但有明显边界 |
| C | 探测安全或兼容占位，语义较浅 |
| D | 局部返回 `-ENOSYS` 或当前不应依赖 |

[SYSCALL.md](SYSCALL.md) 是当前 syscall 支持等级基线；源码中 B/C/D
handler 旁的 `SYSCALL_SUPPORT(...)` 注释锚点记录实现侧的当前语义、
unsupported errno 和后续计划。

主要能力概览：

| 领域 | 当前能力 |
| --- | --- |
| 启动与平台 | OpenSBI、QEMU `virt`、hart 0、SBI 早期控制台、UART MMIO 控制台、Sstc timer |
| 页表与 trap | Sv39、高半区内核、用户页表复制内核高半区、trap frame、syscall、page fault、timer trap、signal/rseq 用户返回工作 |
| 内存管理 | buddy、slab、vmalloc、用户 `mm_struct`、固定数组 VMA、`brk`、匿名和 file-backed `mmap`、`munmap`、`mprotect`、`mremap`、`msync`、`mincore`、`madvise` |
| 调度与同步 | 单核非抢占内核、4 级 MLFQ、timer tick 计费、等待队列、mutex、内核线程 |
| 进程与线程 | PID、`fork` 风格 clone、Linux `clone` 子集、线程组、`execve`、`exit`、`exit_group`、`wait4`、孤儿进程过继 |
| 信号 | `kill`、`tkill`、`tgkill`、`rt_sigaction`、`rt_sigprocmask`、`rt_sigreturn`、`sigaltstack`、用户 signal frame 和 trampoline |
| futex / rseq / membarrier | `FUTEX_WAIT`、`FUTEX_WAKE`、`FUTEX_WAIT_BITSET`、`FUTEX_WAKE_BITSET`、robust list、`set_tid_addr`、单核 rseq 注册/恢复/abort、单核兼容 membarrier |
| VFS 与 fd | fdtable、cwd/root/umask、`openat`、`close`、`read`、`write`、`readv`、`writev`、`pread64`、`pwrite64`、`lseek`、`dup`、`dup3`、`fcntl` 子集 |
| 路径与目录 | `getcwd`、`chdir`、`mkdirat`、`mknodat`、`unlinkat`、`symlinkat`、`linkat`、`readlinkat`、`renameat2` 的 `RENAME_NOREPLACE` 子集 |
| stat / fs 信息 | `newfstatat`、`fstat`、`statfs64`、`fstatfs64`、`statx` 基础字段、ext2 `statfs` |
| poll / epoll | VFS poll wait queue、`ppoll`、`pselect6`、`epoll_create1`、`epoll_ctl`、`epoll_pwait` 的兼容语义 |
| ext2 | 4 KiB block size、direct/single/double indirect 文件映射、目录项、symlink、link/unlink、rename、truncate、fallocate mode 0 |
| 块层与 page cache | 块设备注册、virtio-blk modern MMIO 轮询驱动、统一 4 KiB page cache、dirty list、fsync/msync/writeback、raw block alias 一致性 |
| 时间与资源 | `nanosleep`、`clock_gettime`、`clock_getres`、`clock_nanosleep`、`gettimeofday`、`times`、`getrusage`、`prlimit64`、`sysinfo`、POSIX timer 子集 |
| 身份与杂项 | `getpid`、`getppid`、`gettid`、uid/gid 查询与设置、groups 占位语义、`umask`、`uname`、`getrandom` 弱随机源 |
| 用户态 | `/bin/init`、`/bin/sh`、最小 libc、`ecall` 封装、静态用户 ELF、用户态测试程序 |
| 测试 | kernel self-test、用户态 fs/fd/mm/task/signal/time/poll/rseq/smp 等测试程序 |

## 用户命令和测试程序

`user/bin/*.c` 会被构建为 `/bin/<name>`。当前仓库包含：

```text
cat cmp cp df du echo false find grep head hexdump id kill ls mkdir
pwd rm rmdir stat tail tee touch true uname wc
```

以及面向回归验证的用户态测试程序：

```text
fd_test fs_test mm_test poll_test printk_test rseq_test signal_test
smp_test task_test time_test
```

这些程序依赖当前已支持 syscall 子集，不代表任意 Linux 用户态程序都能运行。

## 暂不支持或语义较浅

以下内容仍是后续目标或明确的浅语义区域：

- SMP、多核 runqueue、IPI、TLB shootdown、真实多核自旋锁语义。
- COW fork、换页、完整 resident pin、完整 `mlock` / `munlock`。
- 动态链接、脚本解释器、完整 libc、运行任意动态 Linux 程序。
- mount namespace、bind mount、propagation、完整动态 `mount` / `umount`。
- 完整 epoll edge/oneshot、嵌套 epoll、poll/select 与信号 race 细节。
- 完整 POSIX signal 行为、`SA_RESTART`、复杂 signal mask/restart 语义。
- 完整 POSIX timer、wall-clock RTC、`clock_settime` 可写实时时钟。
- futex requeue、PI futex、跨进程 shared futex inode key。
- 完整权限/capability/credential/LSM/ACL 模型。
- virtio-blk 中断驱动、多请求队列、UART 中断驱动。
- ext2 大文件 triple-indirect 常规映射、完整崩溃一致性和日志。
- `syslog` ring buffer 读取/清除等完整语义。

更细的 syscall 支持等级和下一步策略见 [SYSCALL.md](SYSCALL.md)。

## 快速开始

需要本机有 RISC-V 交叉工具链、QEMU 和 ext2 镜像工具：

- `riscv64-linux-gnu-*`、`riscv64-unknown-elf-*` 等任一可用工具链；
  也可以通过 `TOOLPREFIX=` 手动指定。
- QEMU `qemu-system-riscv64`，版本至少 7.2。
- `mkfs.ext2` 和 `debugfs`，通常来自 `e2fsprogs`。
- `bc`，用于 Makefile 检查 QEMU 版本。

构建内核：

```sh
make
```

首次构建如果没有 `.config`，构建系统会从
`configs/cuteos_defconfig` 生成基线配置。也可以显式执行：

```sh
make defconfig
make menuconfig
```

构建用户态程序：

```sh
make user
```

生成 ext2 镜像并启动 QEMU：

```sh
make qemu
```

启动后会进入串口 shell。退出 QEMU nographic 控制台可使用 `Ctrl-a x`。

## 常用构建命令

| 命令 | 作用 |
| --- | --- |
| `make` | 按 `.config` 构建内核 ELF |
| `make defconfig` | 用 `configs/cuteos_defconfig` 重置 `.config` |
| `make menuconfig` | 交互式修改构建配置 |
| `make qemu` | 构建镜像并启动 QEMU |
| `make test` | 构建并运行内核自测回归套件 |
| `make qemu-gdb` | 启动 QEMU 并暂停在入口，同时打开 GDB stub |
| `make .gdbinit` | 生成 GDB 启动文件 |
| `make user` | 只构建用户态 ELF |
| `make cuteos.img` | 只构建根文件系统镜像 |
| `make analyze` | 运行 GCC analyzer 和额外诊断 |
| `make tags` | 生成 ctags 索引 |
| `make gtags` | 生成 GNU Global 索引 |
| `make asm` / `make sym` | 生成反汇编或符号表 |
| `make clean` / `make clean-user` | 删除构建产物或用户态构建产物 |

常用变量：

| 变量 | 作用 |
| --- | --- |
| `TOOLPREFIX=<prefix>` | 覆盖 RISC-V 工具链前缀 |
| `V=1` | 打印完整构建命令 |

示例：

```sh
make defconfig
make tags
make menuconfig
TOOLPREFIX=riscv64-linux-gnu- make qemu
```

## 镜像内容

`mkimg.sh` 会创建 ext2 镜像，大小由 `CONFIG_ROOTFS_IMAGE_SIZE_MB` 配置。
镜像内容包括：

- `/init` 和 `/bin/init`
- `/bin/sh`
- `user/bin/*.c` 对应的 `/bin/<name>`
- `/dev/console`
- `/dev/null`

内核启动后通过 virtio-blk 读取该镜像，由 VFS 自动探测文件系统类型并挂载为
根文件系统，再从 VFS 加载用户 ELF。

## 构建系统结构

构建系统是聚合式 Makefile。顶层 `Makefile` 引入：

- `scripts/toolchain.mk`
- `scripts/kconfig.mk`
- `scripts/build.mk`
- `scripts/flags.mk`
- `filelist.mk`

`filelist.mk` 再引入各目录的对象清单，例如 `arch/riscv/arch.mk`、
`kernel/kernel.mk`、`mm/mm.mk`、`fs/fs.mk` 和 `syscall/syscall.mk`。新增内
核源文件时必须把对象加入对应目录的 `*_OBJS`，否则不会进入链接。

内核链接脚本是 `kernel.ld`。内核 ELF 链接在
`KERNEL_VBASE + 0x80200000`，由 QEMU/OpenSBI 加载到物理内存后，`boot.S`
建立临时 Sv39 页表并跳转到高半区 C 入口。

用户程序由 `user/user.mk` 管理，使用 `user/user.ld`，入口基址为
`0x10000`，并按 RX/R/RW 拆分 PT_LOAD 段，避免 text/data 合并成 RWE 段。

详细说明见 [docs/architecture/compile.md](docs/architecture/compile.md)。

## 架构文档

架构文档位于 `docs/architecture/`：

| 文档 | 内容 |
| --- | --- |
| [overview.md](docs/architecture/overview.md) | 总体运行模型、分层结构、关键对象和文档地图 |
| [boot.md](docs/architecture/boot.md) | OpenSBI 到 PID 1 的启动路径 |
| [compile.md](docs/architecture/compile.md) | 构建、链接、Kconfig、用户 ELF 和镜像生成 |
| [trap.md](docs/architecture/trap.md) | trap entry/return、syscall、page fault、timer 和用户返回工作 |
| [pgtable.md](docs/architecture/pgtable.md) | Sv39 地址空间、PTE、内核/用户页表和 TLB |
| [memory.md](docs/architecture/memory.md) | buddy、slab、vmalloc、VMA、mmap、uaccess 和缺页 |
| [task.md](docs/architecture/task.md) | task、clone、exec、exit/wait、signal、futex、rseq、time |
| [sched.md](docs/architecture/sched.md) | 单核 MLFQ、调度点、等待队列和 mutex |
| [syscall.md](docs/architecture/syscall.md) | syscall ABI、分发表、handler 结构和支持策略 |
| [vfs.md](docs/architecture/vfs.md) | VFS 对象、路径解析、fdtable、poll 和 mutation API |
| [ext2.md](docs/architecture/ext2.md) | ext2 mount、inode、目录、block mapping、truncate 和 statfs |
| [block.md](docs/architecture/block.md) | 块设备、page cache、writeback、raw alias 和 virtio-blk |

这些文档描述内部架构和边界。README 只提供面向使用者和贡献者的入口。

## 目录地图

| 目录 | 职责 |
| --- | --- |
| `arch/riscv/` | boot、trap、context switch、timer、SBI、PLIC、Sv39 页表、TLB |
| `init/` | `kernel_main()` 和内核启动编排 |
| `kernel/` | task、fork、exec、exit、wait、signal、futex、rseq、time、worker、printk |
| `sched/` | 调度核心和 MLFQ 策略 |
| `mm/` | buddy、slab、vmalloc、mmap/VMA、page fault、uaccess、user_map |
| `syscall/` | Linux riscv64 syscall ABI 适配层 |
| `fs/vfs/` | super/inode/dentry/file/path/fdtable/mount/read-write/poll |
| `fs/ext2/` | ext2 superblock、inode、目录、文件、块分配 |
| `fs/pipe.c` | 匿名管道 file operations |
| `block/` | 块设备注册、page cache、writeback、virtio-blk |
| `drivers/` | console、UART、virtio 基础定义 |
| `lib/` | freestanding 字符串、格式化、工具代码 |
| `test/` | 内核自测 |
| `user/` | 用户态 init、shell、命令、最小 libc、用户链接脚本 |
| `include/kernel/` | 内核内部 API |
| `include/uapi/` | 内核和用户态共享 ABI 常量与布局 |
| `tools/` / `scripts/` | Kconfig、构建、镜像和辅助工具 |

## 开发提示

- 公共 ABI 统一维护在 `include/uapi/`。新增 syscall 时先确认 Linux
  riscv64 号，再更新 `include/uapi/syscall.h`、
  `include/kernel/syscall_table.h` 和对应 `syscall/` 实现。
- syscall handler 应保持薄封装：解码参数、复制用户指针、调用子系统 API、
  返回负 errno。不要把 VFS、MM、ext2、驱动核心逻辑塞进 `syscall/`。
- 用户指针必须通过 `copy_to_user()`、`copy_from_user()`、
  `strncpy_from_user()`、`user_range_probe()` 或相关 uaccess helper。
- `include/uapi/`、trap frame、signal frame、task layout、用户链接脚本等
  ABI 可见内容必须同步内核、用户态和测试。
- 普通 `make qemu` 启动后进入 shell；内核自测只通过 `make test` 构建并运行，
  且每次运行使用临时 rootfs 镜像副本。
- 改动子系统后，至少运行对应内核自测或用户态集成测试，并在 shell 中复现
  关键路径。
- 新增内核对象时更新对应目录 `.mk`；新增用户命令时放到
  `user/bin/<name>.c`。
- 默认不提交、不推送，除非明确要求。

## 当前路线

近期目标不是继续单纯增加 syscall 数量，而是冻结支持面描述、加深已支持入口
的语义，并清理隐形耦合。当前优先级见 [TODO.md](TODO.md)：

1. 以 [SYSCALL.md](SYSCALL.md) 作为 syscall 支持面基线，避免“入口存在即
   完整支持”的误解。
2. 整理 user-return-work、task 聚合根和 syscall 文件职责。
3. 加深 `fcntl` / `ioctl` / `pipe2` / `openat` 等真实程序高频探测路径。
4. 加深 `mmap` / `mprotect` / `msync` / `madvise` / `mincore`。
5. 稳定 `clone` / `futex` / `rseq` / `membarrier` 的单核兼容语义。
6. 固定 signal、sleep、poll/epoll 被信号打断时的返回行为。
7. 文档化并测试 `statx`、`statfs`、`mount` 的当前边界。

长期方向包括更完整的 Linux ABI 兼容、更多真实静态用户程序、更强文件系统语
义、设备中断驱动、RTC、以及在明确边界后推进 SMP。
