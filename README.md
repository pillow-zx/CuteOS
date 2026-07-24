# cuteOS

cuteOS 是一个面向实验与探索的 RISC-V 64 Unix-like 内核。它运行在 QEMU
`virt` 平台上，经 OpenSBI 启动，使用 Sv39 页表、高半区内核映射和 Linux
riscv64 用户态 ABI。

项目以运行真实的静态 riscv64 ELF 程序为验证目标，参考现代内核的核心设计，
但有意缩减策略、驱动和边缘兼容面。它不是教学内核，也不试图成为 Linux 的
生产级替代品；优先目标是正确的 ABI、清晰的子系统边界、可验证的并发语义和
可调试性。

它也是内核机制的预验证平台：可先以较低集成成本实现并比较理论机制，利用 
Linux ABI 工作负载观察相对效果；通过预验证的设计再移植到 Linux，进行
真实生态下的细致测量与外部有效性验证。cuteOS 的结果不替代 Linux 性能结论。

## 项目定位

当前平台与运行模型：

- RISC-V 64 `rv64gc`、Sv39、S-mode，QEMU `virt` 与 OpenSBI。
- 静态、非 PIE 的 ELF64 RISC-V 用户程序，Linux riscv64 syscall ABI。
- 构建时生成 ext2 根文件系统；VFS 从 virtio-blk 启动根文件系统。
- 单核 4 级 MLFQ；内核目前不可抢占，用户返回点可按 timer 请求切换。
- UART 与 virtio-blk 主要使用轮询 I/O。
- 内核和 syscall 统一返回 Linux 数值的负 errno。

新增代码应隔离架构相关实现：通用策略和生命周期不依赖 RISC-V 细节，汇编、
CSR、页表、trap、timer、IPI 与平台设备访问留在 `arch/` 或明确的驱动边界中。
这是一项持续约束，而非单独的“可移植性阶段”。

## 演进路线

路线按依赖关系排序；前一阶段的语义和测试是后一阶段的基础。

| 阶段 | 目标 | 完成标志 |
| --- | --- | --- |
| 1 | 完善 syscall 支持 | 以真实工作负载和回归测试加深 A/B 级语义 |
| 2 | 完善并发基础 | 原子、锁、内存序、等待/唤醒与 IRQ 规则可在多核下成立 |
| 3 | 实现可抢占内核 | 内核抢占点、抢占计数和锁规则有明确且受测的契约 |
| 4 | 实现最小 SMP 支持 | 多 hart 启动、per-hart 当前任务和 runqueue、IPI 与远程唤醒可用 |
| 5 | 实现 SMP 策略 | affinity、负载均衡、任务迁移与 work stealing 有可观测策略 |

当前重点是阶段 1，同时为阶段 2 避免继续引入“单核即安全”的隐含假设。

## 当前能力与边界

系统已具备 OpenSBI 启动、virtio-blk/ext2 根文件系统、VFS、进程和线程子集、
信号、futex、匿名及文件映射、poll/epoll 兼容入口，以及内置 shell、命令和
用户态测试程序。

`include/kernel/syscall_table.h` 当前安装 110 个 Linux riscv64 syscall
入口。入口存在不等于完整 Linux 兼容；[SYSCALL.md](SYSCALL.md) 的 A/B/C/D
矩阵才是支持承诺和后续优先级的来源。

当前尚未实现的关键能力包括：内核抢占、SMP、IPI/TLB shootdown、完整 Linux
信号与 timer 语义、动态链接、COW fork、交换、生产级安全和广泛设备支持。
这些限制不妨碍实验，但任何新增功能都必须明确其语义边界和测试范围。

## 快速开始

需要 RISC-V GCC 15+、QEMU 7.2+、`e2fsprogs` 和 `bc`。BusyBox profile
额外要求 Zig 0.16+，用于生成严格 `lp64` 的 compiler-rt builtins。可通过
`TOOLPREFIX=<prefix>` 指定交叉工具链。

```sh
make defconfig
make qemu
```

默认 profile 使用项目 minimal libc、内置 shell、命令和 ABI 测试程序。要使用
静态 musl BusyBox：

```sh
make busybox_defconfig
make qemu
```

BusyBox profile 仍由项目 minimal-libc `/init` 作为 PID 1，只将 `/bin/sh` 和
用户命令切换为 BusyBox。两种 profile 都只生成 static、non-PIE、soft-float
`lp64` ELF。用户态 ISA 固定为 `rv64imac_zicsr_zifencei`，禁止生成 F/D 指令；
浮点需求由 Zig compiler-rt soft-float builtins 处理。动态链接、PIE 和用户 FPU
上下文不属于当前运行时路线。

QEMU 启动后进入串口 shell；使用 `Ctrl-a x` 退出。常用命令：

| 命令 | 作用 |
| --- | --- |
| `make` | 构建内核 ELF |
| `make user` | 构建用户态 ELF |
| `make busybox_defconfig` | 选择静态 musl BusyBox 用户态 profile |
| `make qemu` | 构建镜像并启动 QEMU |
| `make test` | 构建并运行内核自测回归套件 |
| `make qemu-gdb` | 启动带 GDB stub 的 QEMU |
| `make menuconfig` | 修改配置 |
| `make analyze` | 运行 GCC analyzer 和额外诊断 |

## 文档与代码导航

- [CONTEXT.md](CONTEXT.md)：架构边界、稳定入口、当前并发模型和修改导航。
- [SYSCALL.md](SYSCALL.md)：syscall 成熟度、语义边界和优先级。
- `docs/architecture/`：boot、trap、调度、内存、VFS、block 和 ext2 的详细设计。
- [AGENTS.md](AGENTS.md)：贡献和自动化修改规则。

新增源文件必须进入相应 `*.mk` 对象列表。修改用户可见 ABI 时，须同步检查
`include/uapi/`、用户态镜像、Linux riscv64 头文件和相关测试。
