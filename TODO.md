# cuteOS 下一阶段 TODO 计划

本文把下一阶段路线拆成可执行阶段。目标不是继续堆 syscall 数量，而是：

1. 优化架构布局，降低耦合度，减少隐形要求。
2. 扩大当前已支持 syscall 的功能语义。
3. 适时添加新的架构、平台和设备支持。

## 阶段 0：冻结支持面描述

目标：让后续工作有清晰基线，避免“入口存在即完整支持”的误解。

- [x] 把 [SYSCALL.md](./SYSCALL.md) 作为支持面基线。
- [x] 在 README 或 CONTEXT 中增加“syscall 支持等级”入口，链接矩阵。
- [x] 为每个 B/C/D syscall 建一个测试或注释锚点：当前语义、unsupported errno、未来计划。（本轮按注释锚点完成，暂不建立测试。）
- [x] 更新 `sys_time.c` 头注释，删除“sys_timer_* 均返回 -ENOSYS”的过期描述。

验收：

- `rg "sys_timer_\\*.*ENOSYS|均返回 -ENOSYS" syscall docs README.md CONTEXT.md` 不再误导。
- 矩阵覆盖 `include/kernel/syscall_table.h` 的 110 个入口。

## 阶段 1：架构边界整理

目标：先拆隐形耦合，再继续加深语义。

### 1.1 建立 user-return-work 边界

- [x] 新增 generic 用户返回前工作入口，例如 `kernel/user_return.c`。
- [x] 定义顺序：rseq resume、signal delivery、未来 syscall restart、其它 pending work。
- [x] `arch/riscv/trap.c` 只在用户 trap 返回前调用该入口。
- [x] 增加测试覆盖：ecall、page fault、timer interrupt 三条路径都执行同一返回工作。

验收：

- `arch/riscv/trap.c` 不直接编排 rseq + signal 的顺序。
- rseq/signal 现有用户态测试仍通过。

### 1.2 收窄 task 聚合根暴露面

- [x] 制定 `task_struct` 字段归属规则：生命周期聚合可放 task，复杂语义回 owning subsystem。
- [x] 审查 `include/kernel/task.h` 的 accessor，标记热路径、跨子系统必需、可下沉三类。
- [x] rseq 操作尽量通过 `include/kernel/rseq.h` 暴露，减少外部直接操作 task rseq 字段。

验收：

- 新增 per-task 状态必须说明 owner 和生命周期。
- `task.h` 不再为单一子系统暴露大批非通用 helper。

### 1.3 清理 syscall 文件职责

- [x] 把 `sys_rseq()` 移出 `sys_stub.c` 到 `syscall/sys_rseq.c` 或明确归属文件。
- [x] 把 affinity syscall 移到 `syscall/sys_sched.c`。
- [x] 保留 `sys_stub.c` 只承载真正探测安全或 unsupported 的入口。

验收：

- `sys_stub.c` 中不存在已经有核心子系统实现的 syscall。
- 对应 `syscall/syscall.mk` 已更新。

## 阶段 2：已支持 syscall 语义加深

目标：优先让真实软件更少撞到浅语义，而不是先加新 syscall。

### 2.1 fd、fcntl、ioctl、pipe

- [x] 建立 `fcntl` cmd 支持表，明确 unsupported errno。
- [x] 扩展 `pipe2` 支持 `O_NONBLOCK`，或明确拒绝并测试。
- [x] 补 tty/console 常用 ioctl：窗口大小、termios 探测、必要错误码。
- [x] 将 `fdatasync` 从 `fsync` 别名拆出，增加 FS datasync hook 与 fallback，覆盖数据写回且跳过纯 inode 元数据的测试。

验收：

- `fd_test` 覆盖 `F_GETFL/F_SETFL/F_DUPFD_CLOEXEC/O_NONBLOCK`。
- 常见 shell/coreutils 的 ioctl 探测不产生误导性成功。

### 2.2 mmap/mprotect/msync/madvise

- [x] 明确 file-backed mmap 的 MAP_PRIVATE/MAP_SHARED 支持等级。
- [x] 加强 `msync` 与 page cache/writeback 的连接。
- [x] 为 `madvise` 建立 advice 支持表。
- [x] 覆盖跨 VMA `mprotect`、partial `munmap`、`mremap` flag 组合。

验收：

- `mm_test` 覆盖匿名映射、文件映射、权限切换、DONTNEED、mincore。
- 所有 unsupported flag 的 errno 固定。

### 2.3 clone/futex/rseq/membarrier

- [x] 建立 clone flag 支持表，明确 namespace/vfork/io/parent 等拒绝原因。
- [x] futex 扩展按 pthread 实际需求排序：WAIT_PRIVATE/WAKE_PRIVATE 已有则固定，requeue/bitset 视需求添加。
- [x] rseq 明确 flag 语义：支持、忽略、拒绝三选一，并补测试。
- [x] membarrier 标注单核兼容语义，SMP 前不宣传完整 expedited 行为。

验收：

- `task_test`、`rseq_test`、`smp_test` 覆盖生命周期和错误路径。
- libc 线程探测路径行为稳定。

### 2.4 signal 和 sleep/poll 中断语义

- [x] 建立 signal action flag 支持表。
- [x] 明确 `SA_RESTART` 当前策略，并选择实现或固定不支持行为。
- [x] 增加 sleep、ppoll、pselect、epoll_pwait 被 signal 打断的测试。
- [x] 继续扩展非法 signal frame 和 sigreturn 安全测试。

验收：

- `signal_test` 和 `poll_test` 覆盖 signal interruption。
- sleep/poll 返回 `-EINTR` 和 remainder 行为固定。

### 2.5 statx/statfs/mount

- [x] statx mask 支持范围文档化，只报告真实支持字段。
- [x] ext2 statfs 字段补齐或明确置零字段。
- [x] mount/umount 先定义最小模型：单 namespace、ext2、无 bind、无 propagation。
- [x] 若暂不加深 mount，则把等级保持 C 并测试 unsupported flag。

验收：

- `fs_test` 覆盖 stat/statx/statfs 基础字段。
- mount/umount 行为不对真实软件伪装成完整 Linux。

## 阶段 3：平台和设备边界

目标：新增平台前先让 arch/platform/device 边界可替换。

### 3.1 单核假设清单

- [ ] 列出当前单核假设：scheduler、affinity、membarrier、rseq、timer、spinlock、interrupt。
- [ ] 每项标注未来 SMP 替换点。
- [ ] 在 `CONTEXT.md` 中链接该清单。

验收：

- 新增 SMP 相关工作前，有明确影响面。

### 3.2 platform ops

- [ ] 抽象 timer、console、interrupt controller、block discovery 的 platform 入口。
- [ ] RISC-V QEMU virt 保持现有默认实现。
- [ ] 不在 syscall 或 VFS 中引入平台判断。

验收：

- 新平台只改 `arch/`、`drivers/` 和 platform glue，不改通用 syscall 语义。

### 3.3 设备支持顺序

- [ ] 优先 UART/console ioctl 语义。
- [ ] 再补 virtio-blk 中断驱动或更完整队列。
- [ ] 再考虑 RTC，用于 REALTIME/gettimeofday/clock_settime。
- [ ] 最后考虑网络设备，因为会拉入 socket syscall 和协议栈。

验收：

- 每新增一个设备，都有对应 syscall/用户程序探测路径测试。

## 阶段 4：回归和发布门槛

目标：让“语义加深”不会破坏已有可运行软件。

- [ ] 建立用户态测试分组：fs、fd、mm、task、signal、time、poll、rseq、smp。
- [ ] 每个 B -> A 的 syscall 升级必须有用户态测试。
- [ ] 每个 unsupported -> partial 的 syscall 必须记录 errno 变化。
- [ ] 每轮架构整理后运行 `make`、`make user`，并在 QEMU 中运行相关用户态测试。

建议基础命令：

```sh
make
make user
```

建议 QEMU 内测试：

```sh
fd_test
fs_test
mm_test
task_test
signal_test
time_test
poll_test
rseq_test
smp_test
```

## 近期三步建议

1. 先做 `user_return` generic helper，不改变用户可见行为，只降低后续耦合。
2. 再拆 `sys_stub.c`，把 rseq/affinity 放到正确文件。
3. 然后从 `fcntl/pipe2/ioctl` 和 `mmap/msync/madvise` 两组开始补语义，因为它们对真实软件启动和运行影响最大。
