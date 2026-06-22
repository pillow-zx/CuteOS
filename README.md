# cuteOS

cuteOS 是一个面向教学和自研实验的 RISC-V 64 类 Unix 内核。它运行在
QEMU `virt` 平台上，经 OpenSBI 启动，使用 Sv39 页表、高半区内核映射和
Linux riscv64 用户态 ABI，目标是逐步跑起无需为 cuteOS 特别适配的静态
Linux riscv64 ELF。

当前仓库已经能构建内核、生成 ext2 根文件系统镜像、启动到 PID 1
`/bin/init`，再进入交互式 `/bin/sh`。shell 可以运行仓库内置的用户态命令，
支持基本文件操作、进程创建、管道、重定向、信号和匿名内存映射。

## 项目定位

cuteOS 不是 Linux，也不是只会打印日志的 boot demo。它是一颗尽量小、但按
真实二进制接口推进的教学内核：

- 架构：RISC-V 64，`rv64gc`，Sv39。
- 平台：QEMU `virt`，默认内存 256 MB；QEMU CPU 数来自 Kconfig，
  内核当前仍仅启动 hart0。
- 启动链路：OpenSBI -> 高半区内核 -> `kernel_main` -> PID 1 init ->
  `/bin/sh`。
- 用户态契约：系统调用号、错误码、`stat`、信号 ABI 等按 Linux riscv64
  约定组织；未实现系统调用返回 `-ENOSYS`。
- 文件系统：构建时生成 ext2 镜像，默认 16 MB，内核启动后挂载为根文件系统。
- 取舍：优先选择最小正确实现。比如 fork 目前急切复制，无 COW；virtio-blk
  和 UART 当前为轮询；buffer cache 写穿；内核非抢占，用户态由时钟中断触发
  时间中断触发发时间片调度。

项目的目标是跑起真实 Linux riscv64 静态 ELF，达到 busybox 级的交互
体验：shell 可用、管道可用、ext2 文件可读写、系统调用按 Linux ABI 返回。
它面向已经接触过 xv6 或 rCore、想继续理解真实 ABI 和用户态程序如何落到
内核实现上的学习者。

核心设计规则如下：

- Linux riscv64 ABI 是内核与用户态的唯一契约；系统调用号、错误码、信号和
  `stat` 等二进制布局都围绕这个契约组织。
- 内核和用户态是两个 freestanding 世界，互不链接；两边只通过 ecall ABI 和
  ext2 镜像汇合。
- 内核运行在高半区，使用 Sv39 页表和直接映射访问物理内存；用户页表保留
  内核高半区映射，陷入后无需切换到另一套内核页表。
- 内存分配按 buddy -> slab -> 用户 VMA/缺页的层次推进，下层不反向依赖上层。
- 文件系统经 VFS 的 super/inode/file ops 与 ext2 解耦；pipe 则作为匿名
  `file_operations` 接入文件描述符层。
- 当前坚持单核、非抢占和轮询 I/O，先把行为契约做正确，再逐步替换内部实现。
- 内核返回错误统一使用 Linux 数值的负 errno，不引入新的错误约定。

## 能做什么

启动后，cuteOS 会进入一个小型 shell：

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

它可以：

- 从 ext2 镜像加载 `/init`、`/bin/sh` 和用户命令 ELF。
- 在用户态通过 `ecall` 调用 Linux riscv64 编号的系统调用。
- 运行 shell 和基础 coreutils 风格命令。
- 创建进程和线程，执行 `fork` / `clone` / `execve` / `wait4`。
- 使用匿名管道、文件描述符复制、输入输出重定向。
- 读写 ext2 文件、目录和设备节点。
- 使用 `brk`、匿名 `mmap`、`munmap` 和按需缺页分配。
- 注册和投递信号，支持用户态信号处理器和 `sigreturn` 蹦床。
- 运行内核自测和用户态集成测试程序。

## 支持范围

以下是 cuteOS 支持的主要能力：

| 领域 | 支持内容 |
| --- | --- |
| 启动与平台 | OpenSBI 启动、QEMU `virt`、高半区内核、Sv39、SBI 早期控制台、UART MMIO 控制台、Sstc 定时器 |
| 内存管理 | buddy 物理页分配、slab 对象分配、用户页表、VMA、`brk`、匿名 `mmap`、`munmap`、按需缺页、`copy_to_user` / `copy_from_user` |
| 调度 | 单核非抢占调度、4 级 MLFQ、时间片降级、周期 boost、等待队列、内核线程、单核 CPU affinity 语义 |
| 进程与线程 | PID 分配、`task_struct`、`fork`、Linux 风格 `clone` 子集、线程组、`execve`、`exit` / `exit_group`、`wait4`、孤儿进程过继 |
| 文件描述符 | `openat`、`close`、`read`、`write`、`readv`、`writev`、`pread64`、`pwrite64`、`lseek`、`dup`、`dup3`、`fsync`、`fdatasync`、`ftruncate64` |
| VFS 与存储 | VFS inode/dentry/file 抽象、ext2 读写、目录遍历、路径解析、根文件系统挂载、virtio-blk 轮询驱动、buffer cache 写穿 |
| 文件系统接口 | `mkdirat`、`unlinkat`、`mknodat`、`getcwd`、`chdir`、`faccessat`、`getdents64`、`newfstatat`、`fstat`、`statfs64`、`fstatfs64`、`readlinkat` |
| 管道 | 匿名 pipe、阻塞读写、EOF、无读者写入返回 `-EPIPE` |
| 信号 | `kill`、`tgkill`、`rt_sigaction`、`rt_sigprocmask`、用户态信号帧、trampoline `sigreturn`、`SIGKILL` / `SIGSTOP` / `SIGCHLD` / `SIGPIPE` 等基础语义 |
| 时间与身份 | `times`、`gettimeofday`、`clock_gettime`、`clock_getres`、`nanosleep`、`clock_nanosleep`、`uname`、`getpid`、`getppid`、`gettid`、uid/gid 查询与设置、`umask`、`sysinfo` |
| futex | `FUTEX_WAIT`、`FUTEX_WAKE`、超时等待、robust list 注册和线程退出唤醒 |
| 用户态 | `/bin/init`、`/bin/sh`、最小 libc、`ecall` 封装、`printf` / string / stdlib 子集 |
| 用户命令 | `cat`、`cp`、`echo`、`false`、`id`、`kill`、`ls`、`mkdir`、`pwd`、`rm`、`rmdir`、`stat`、`touch`、`true`、`uname`，以及依赖已支持 syscall 的小型命令 |
| 测试 | `CONFIG_KERNEL_TEST=y` 时启用内核自测；覆盖 bitmap、PID、buddy、slab、trap、timer、sync、MM/VMA、task、资源复制、调度、内核线程、virtio-blk、buffer cache；另有 `thread-test` 和 `vma-test` 用户态集成测试 |

### 暂不支持或后置

以下能力当前仍属于后置目标：

- SMP、多核启动、per-CPU、IPI、真实自旋锁扩展。
- COW fork、page cache、换页、`mlock` / `munlock` 的完整内存驻留语义。
- 动态链接、完整 libc、运行任意动态 Linux 程序。
- mount namespace、动态 `mount` / `umount`、多文件系统挂载。
- `epoll`、完整 `poll` / `select` 族、异步 I/O。
- 外部中断驱动的 virtio/UART、完整 PLIC 接入。
- `renameat2`、完整硬链接/软链接维护、大文件三级间接块。
- 完整 POSIX timer、interval timer、RTC wall-clock。

## 快速开始

需要本机有 RISC-V 交叉工具链、QEMU 和 ext2 镜像工具：

- `riscv64-linux-gnu-*`、`riscv64-unknown-elf-*` 等任一可用工具链；
  也可以用 `TOOLPREFIX=` 手动指定。
- QEMU `qemu-system-riscv64`，版本至少 7.2。
- `mkfs.ext2`、`debugfs`，通常来自 `e2fsprogs`。
- `bc`，用于 Makefile 检查 QEMU 版本。

构建内核：

```sh
make
```

首次构建如果没有 `.config`，构建系统会自动从
`configs/cuteos_defconfig` 生成基线配置。修改配置请使用：

```sh
make defconfig
make menuconfig
```

其中：

- `make defconfig` 会用 `configs/cuteos_defconfig` 显式覆盖根目录 `.config`
- `make menuconfig` 会在当前 `.config` 基础上交互式修改配置

构建用户态程序：

```sh
make user
```

生成镜像并启动 QEMU：

```sh
make qemu
```

启动后会进入串口 shell。退出 QEMU 可在 nographic 控制台中使用
`Ctrl-a x`。

## 常用构建命令

| 命令 | 作用 |
| --- | --- |
| `make` | 按 `.config` 构建内核 ELF，默认输出到 `build/kernel/` |
| `make defconfig` | 用 `configs/cuteos_defconfig` 重置根目录 `.config` |
| `make menuconfig` | 修改唯一的根目录 `.config` |
| `make qemu` | 构建内核、生成 ext2 镜像并启动 QEMU |
| `make qemu-gdb` | 启动 QEMU 并暂停在入口，同时打开 GDB stub |
| `make print-gdbport` | 打印当前 GDB stub 端口 |
| `make user` | 只构建用户态 ELF |
| `make tags` | 生成 `tags` 文件，便于编辑器 / LSP 做源码跳转 |
| `make gtags` | 生成 `GTAGS` / `GRTAGS` / `GPATH`，便于 GNU Global 建索引 |
| `make asm` / `make sym` | 生成反汇编和符号表 |
| `make clean` | 删除构建产物和 `.gdbinit` |

构建系统是聚合式 Makefile：顶层 `Makefile` include 各目录的 `*.mk`，
由顶层统一编译链接；配置由 Kconfig 生成的 `.config`、`auto.conf` 和
`autoconf.h` 驱动。新增内核对象时，把对象文件加入对应目录的 `*_OBJS`；
新增用户命令时，添加 `user/bin/<name>.c` 即可被自动收集进 `/bin/<name>`。

## 镜像内容

`mkimg.sh` 会创建一个 ext2 镜像，大小由
`CONFIG_ROOTFS_IMAGE_SIZE_MB` 配置，并写入：

- `/init` 和 `/bin/init`
- `/bin/sh`
- `user/bin/*.c` 对应的 `/bin/<name>`
- `/dev/console`
- `/dev/null`

内核启动后从 virtio-blk 设备读取镜像，挂载为根文件系统，再从 VFS 加载用户
ELF。

## 目录地图

| 目录 | 职责 |
| --- | --- |
| `arch/riscv/` | boot、trap、context switch、timer、SBI、Sv39 页表、TLB |
| `init/` | `kernel_main` 和内核启动编排 |
| `kernel/` | task、fork、exec、exit、wait、signal、time、futex、printk |
| `sched/` | 调度核心和 MLFQ 策略 |
| `mm/` | buddy、slab、mmap/VMA、page fault、uaccess、vmalloc 占位 |
| `syscall/` | 系统调用分发表和 proc/file/mm/signal/misc/stub 实现 |
| `fs/vfs/` | VFS inode/dentry/file/super/namei/read-write |
| `fs/ext2/` | ext2 superblock、inode、目录、文件、块分配 |
| `fs/pipe.c` | 匿名管道 |
| `block/` | virtio-blk、块设备注册、buffer cache |
| `drivers/` | UART 和 virtio 设备定义 |
| `lib/` | freestanding 内核共享库 |
| `test/` | 内核自测 |
| `user/` | 用户态 init、shell、命令、最小 libc、用户态链接脚本 |
| `include/` | 内核、架构、驱动和编译器头文件 |

## 开发提示

- 系统调用号遵循 Linux riscv64 ABI。新增 syscall 时先在
  `include/kernel/syscall.h` 定号和声明，再在 `syscall/syscall.c` 注册。
- 用户态 ABI 头在 `user/libc/minimal/include/user.h`，和内核头有意保持边界
  重复；修改结构布局时需要同步两侧。
- 内核返回错误统一使用负 errno，例如 `-EINVAL`、`-ENOMEM`、`-ENOSYS`。
- `CONFIG_KERNEL_TEST=y` 时，启动后自动运行内核自测。
- `make defconfig` 用于恢复基线配置，`make menuconfig` 用于交互式调整；`ext2` 仍是唯一可启动根文件系统。
- C 代码使用固定宽度整数类型，避免裸 `unsigned`；格式以 tab 缩进、80 列、
  K&R 风格括号和右指针为主。
- 加新内核对象时，在对应目录的 `*.mk` 中登记对象；加新用户命令时，放到
  `user/bin/<name>.c`，构建系统会自动生成 `/bin/<name>`。
- 改动子系统后，至少运行对应内核自测或用户态集成测试，并在 shell 中复现
  关键路径。
- 这个仓库按子系统粒度组织提交；默认不主动提交，除非明确需要提交或推送。

## 当前路线

cuteOS 目标是跑起真实 Linux riscv64 静态 ELF，最终达到 busybox 级
交互体验：shell 可用、管道可用、ext2 文件可读写、系统调用按 Linux ABI
返回。当前代码已经打通从 boot 到 shell 的主链路，下一步主要是补齐更多
busybox 常用 syscall、提升文件系统语义、完善时间/资源统计，并逐步降低
内存和 I/O 子系统中的简化。
