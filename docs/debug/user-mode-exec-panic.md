# CuteOS 用户态切换与 `exec_user_binary()` 崩溃调查报告

本文记录最小用户态 bring-up 过程中出现的一组 panic，包括最初的
`origin=kernel scause=0x2` 非法指令异常、后续的 `origin=user scause=0xc`
指令页错误，以及最终修复后的预期行为。

这不是单一 bug，而是 4 个彼此叠加的问题：

1. `sstatus.SPP` 被错误地保留为 `1`，`sret` 返回到了 S-mode。
2. `trap_frame` 被放在仍在使用的 C 调用栈顶部，随后被栈帧覆盖。
3. `sys_write()` 在 S-mode 下直接解引用用户指针，但未开启 `SUM`。
4. 用户代码复制到新页后缺少 `fence.i`，存在 I-cache / 取指一致性隐患。

修复后，系统已经可以完成以下路径：

```text
kernel_thread(init_process)
  -> exec_user_binary()
  -> __trapret
  -> sret 进入用户态
  -> user _start
  -> main()
  -> write(1, "Hello CuteOS!\n", 14)
  -> ecall
  -> trap_handler()
  -> do_syscall()
  -> sys_write()
```

运行结果为成功输出 `Hello CuteOS!`，随后在 `SYS_exit` 上按当前最小实现进入
`panic("user exit with code 0")`。这说明本次需要修复的“异常 panic”已经消失，
剩余 panic 属于当前设计预期。

---

## 1. 方案完成度评估

对照本次方案，当前工作区的核心链路已经完整落地：

| 方案项 | 当前状态 | 说明 |
|--------|----------|------|
| `user/start.S` 用户入口 | 已完成 | `_start -> main() -> SYS_exit`，见 `user/start.S` |
| `user/include/user.h` 系统调用封装 | 已完成 | 提供 `syscall0/syscall1/syscall3`、`write()`、`exit()` |
| `user/init/init.c` 测试程序 | 已完成 | 输出 `Hello CuteOS!\n`，长度实际为 `14` 字节，不是 `15` |
| `user/user.ld` 固定链接到 `0x10000` | 已完成 | `.text.entry` 位于最前 |
| `user/Makefile` 生成 `init.bin` | 已完成 | 顶层 `Makefile` 已在内核构建前触发 |
| `arch/riscv/user_elf.S` 内嵌 `init.bin` | 已完成 | 导出 `_user_init_start/_end` |
| `kernel/init_process.c` 调用 `exec_user_binary()` | 已完成 | PID 1 启动用户态测试程序 |
| `kernel/exec.c` 创建用户页表并切到用户态 | 已完成 | 修复后能正确进入 U-mode |
| `arch/riscv/trap.c` 处理 `EXC_ECALL_U` | 已完成 | `sepc += 4` 后调用 `do_syscall()` |
| `syscall/syscall.c` 注册 `SYS_write` / `SYS_exit` | 已完成 | `write` 已工作，`exit` 仍按方案 `panic` |
| `map_page()` 导出供 `exec` 使用 | 已完成 | 实际导出位置在 `include/asm/pte.h`，不是方案摘要中的 `include/asm/page.h` |
| 顶层构建系统先构建用户程序 | 已完成 | `Makefile` 已加入依赖链 |

结论：

- 从“文件是否齐全、主流程是否打通”的角度看，方案已经基本完成。
- 本次 panic 的根因不在“缺文件”或“漏接流程”，而在切换用户态时的几个细节实现错误。

---

## 2. 原始错误现象

最初运行时，内核输出如下：

```text
page_table: mapping 256MB DRAM with 4KB pages...
page_table: switched to kernel page table (pgd=0x000000008021c000, early_alloc=520KB)
uart: init successfully
buddy: 64354 pages free (251 MB)
slab: 8 caches initialized (16..2048 bytes)
mm: init successfully
stvec: 0xffffffc080200090, sscratch: 0x0, sie: 0x20, sstatus: 0x8000000200006022
trap: init successfully
pid: bitmap initialized (256 PIDs, 0 reserved for idle)
task: idle (PID 0) created
task: init successfully
timer: init successfully
sched: runqueue initialized
sched: init successfully
syscall: initialized (256 entries)
syscall: init successfully
task: kernel thread (PID 1) created, fn=0xffffffc0802033ce
init running (PID 1)
exec: loading user binary (332 bytes)
exec: switching to user mode (sepc=0x0000000000010000, sp=0x0000000080000000, pgd=0x000000008049f000)

KERNEL PANIC: unhandled exception: origin=kernel scause=0x2 code=2 sepc=0xffffffc0802127ec stval=0x000000000000203a
  sepc   = 0xffffffc0802127ec
  scause = 0x0000000000000002
  stval  = 0x000000000000203a
  ra     = 0xffffffc08020130e
  sp     = 0xffffffc0804a1e68
```

关键信息有 3 点：

1. `exec` 已经执行到“切换到用户态”前的最后一步。
2. 异常来源是 `origin=kernel`，而不是 `origin=user`。
3. `sepc` 不是方案期望的用户入口 `0x10000`，而是一个高地址
   `0xffffffc0802127ec`，明显仍在内核地址空间。

此外，`scause=0x2` 表示 `Illegal Instruction`，`stval=0x203a` 很像触发异常的
错误指令编码片段，而不是一个正常的取指地址。这通常意味着 CPU 已经跳到错误位置，
并把一段垃圾字节当成了指令。

---

## 3. 设计期望的正确执行路径

本阶段的期望路径应当是：

```text
kernel_main()
  -> kernel_thread(init_process, NULL)
  -> init_process()
  -> exec_user_binary(_user_init_start, size)
      -> 创建用户页表
      -> 映射 0x10000 用户代码页
      -> 映射 0x7FFFF000 用户栈页
      -> 构造 trap_frame
      -> satp 切换到用户页表
      -> 跳转 __trapret
      -> sret
  -> 用户态 _start (0x10000)
      -> main()
      -> write()
      -> ecall
  -> trap_handler()
      -> do_syscall()
      -> sys_write()
```

对应实现位置如下：

- `kernel/init_process.c`：PID 1 调用 `exec_user_binary()`。
- `kernel/exec.c`：分配页表、映射用户代码/栈、构造返回现场。
- `arch/riscv/entry.S`：`__trapret` 恢复 `trap_frame` 并执行 `sret`。
- `user/start.S`：用户程序 `_start -> main -> ecall SYS_exit`。
- `arch/riscv/trap.c`：处理 `EXC_ECALL_U`。
- `syscall/syscall.c`：`do_syscall()` 分发 `SYS_write` / `SYS_exit`。

因此，最初的 panic 说明问题发生在：

```text
exec_user_binary() 末尾
  -> __trapret
  -> sret
```

这一小段极其敏感的“返回现场恢复”路径上。

---

## 4. 调试过程中的三个现场

### 4.1 现场一：`origin=kernel scause=0x2`

最初现场如上，表现为：

- `origin=kernel`
- `scause=2`（非法指令）
- `sepc` 落在高地址内核空间，而不是用户入口 `0x10000`

这说明 CPU 根本没有按预期以 U-mode 从 `0x10000` 开始执行。

### 4.2 现场二：`origin=user scause=0xc`

修正 `SPP` 和 scratch slot 后，新的现场变成：

```text
KERNEL PANIC: unhandled exception: origin=user scause=0xc code=12 sepc=0xffffffc08049bf50 stval=0xffffffc08049bf50
  sepc   = 0xffffffc08049bf50
  scause = 0x000000000000000c
  stval  = 0xffffffc08049bf50
```

这一变化非常关键：

- `origin=user` 说明 `sret` 已经真正返回到 U-mode。
- `scause=12` 是 `Instruction Page Fault`。
- `sepc/stval` 指向 `0xffffffc08049bf50`，这是一个内核高地址，而且看起来很像
  内核栈区域地址。

这表明：用户态确实启动了，但恢复出来的用户 `PC` 已经被破坏，跳到了一个不属于
 用户地址空间的高地址，于是以“用户态取内核栈指令”的形式崩溃。

### 4.3 现场三：修复后输出 `Hello CuteOS!`

最终修复后，QEMU 输出为：

```text
init running (PID 1)
exec: loading user binary (332 bytes)
exec: switching to user mode (sepc=0x0000000000010000, sp=0x0000000080000000, pgd=0x0000000080499000)
Hello CuteOS!

KERNEL PANIC: user exit with code 0
  sepc   = 0x0000000000010010
  scause = 0x0000000000000008
  stval  = 0x0000000000000000
```

此时：

- 用户程序已经从 `0x10000` 正常运行。
- `write()` 已经通过 `ecall` 成功进入内核并输出字符串。
- `sepc=0x10010` 正好落在用户程序里的 `ecall` 指令之后。
- 最终 panic 是 `SYS_exit` 的当前占位实现，不再是异常错误。

---

## 5. 根因分析

本次问题不是单点错误，而是多个细节叠加。

### 5.1 根因一：`SPP` 被错误设置为 `1`

在初始版本的 `kernel/exec.c` 中，`trap_frame->sstatus` 被设置成了：

```c
tf->sstatus = SSTATUS_SPP | SSTATUS_SPIE;
```

这意味着：

- `SPP=1`
- `sret` 会返回到 S-mode

但本阶段设计要求是进入用户态，所以正确设置应当是：

```c
tf->sstatus = SSTATUS_SPIE;
```

也就是：

- `SPP=0`
- `sret` 返回 U-mode

这个问题解释了为什么最初 panic 的 `origin` 是 `kernel`。
哪怕其他内容都正确，只要 `SPP=1`，也不可能进入用户态。

### 5.2 根因二：`trap_frame` 被放在仍在使用的 C 栈顶部

这是本次最致命、也最隐蔽的问题。

#### 5.2.1 `__alltraps` 的用户态入口约束

`arch/riscv/entry.S` 的 U->S trap 路径约定：

1. `sscratch` 中保存“内核栈顶 / scratch slot”指针。
2. trap 发生时，先把用户 `sp` 写到 `sscratch` 指向的位置。
3. 再把 `sp` 切到这个内核栈区域，并在其下方构造 `trap_frame`。

相关代码见：

```asm
.Lfrom_user:
    sd      sp, (0)(t0)
    mv      sp, t0
    addi    sp, sp, -(35*8)
```

这里的含义非常明确：`sscratch` 指向的那 8 字节 scratch slot 之下，必须是一个
完整、未被其他栈帧占用的 trap 保存区。

#### 5.2.2 初始实现的问题

初始实现把 `trap_frame` 直接放在：

```text
current->kstack + KSTACK_SIZE - sizeof(struct trap_frame)
```

也就是“整个内核栈顶端”。

这看起来合理，但实际上 `exec_user_binary()` 自己就是在这个内核栈上运行的 C 函数。
只要它后面还会继续调用函数、保存寄存器、扩展栈帧，这片区域就不是空闲的。

更糟糕的是，初始代码在构造 `tf` 后还调用了 `printk()`，而函数本身也仍然拥有活动的
栈帧。结果就是：

1. 刚写好的 `trap_frame` 放在“看似栈顶、实则仍可能被使用”的区域。
2. 后续 `printk()` 调用和本函数剩余执行过程继续写当前内核栈。
3. `trap_frame` 中的 `sepc/sp/sstatus/ra` 等字段被覆盖。
4. `__trapret` 从被污染的 `trap_frame` 恢复寄存器。
5. CPU 跳到错误的地址执行。

这正好解释了两个现场：

- 现场一中，错误返回现场再叠加 `SPP=1`，变成 `origin=kernel` 的非法指令异常。
- 现场二中，`SPP` 修正后已经回到 U-mode，但损坏的 `sepc` 仍然把 CPU 引向
  内核栈高地址，最终触发 `Instruction Page Fault`。

#### 5.2.3 正确做法

修复后的思路是：

1. 先完成所有可能发生函数调用的工作，例如 `printk()`。
2. 在真正切换前，读取当前 `sp`。
3. 把 `trap_frame` 放在“当前 `sp` 之下的未使用区域”。
4. 在 `trap_frame` 顶部额外预留 8 字节 scratch slot。
5. 构造完 `trap_frame` 后，立刻进入内联汇编切换 `satp`、设置 `sscratch`、
   跳转 `__trapret`，中间不再发生任何 C 调用。

修复后的关键逻辑位于 `kernel/exec.c`：

```c
uintptr_t cur_sp;
__asm__ volatile("mv %0, sp" : "=r"(cur_sp));
cur_sp &= ~(uintptr_t)0xF;

uintptr_t kstack_top = cur_sp - sizeof(uintptr_t);
struct trap_frame *tf =
    (struct trap_frame *)(kstack_top - sizeof(struct trap_frame));
```

这是本次真正打通用户态切换的关键修复。

### 5.3 根因三：未为 `__alltraps` 预留 scratch slot

`arch/riscv/entry.S` 的 U->S 路径第一条关键写入是：

```asm
sd      sp, (0)(t0)
```

其中 `t0` 来自 `sscratch`。

如果 `sscratch` 直接指向 `trap_frame` 之上的紧邻区域，而没有明确预留 8 字节 scratch
slot，那么这次写入就会碰撞到其他数据。这个问题在“`trap_frame` 本身放置错误”的
大背景下更容易放大后果。

修复后，`sscratch` 明确指向 `kstack_top`，而真正的 `trap_frame` 位于其下方：

```text
高地址
  [8B scratch slot]    <- sscratch
  [trap_frame]
低地址
```

这与 `__alltraps` 的汇编约定一致。

### 5.4 根因四：`sys_write()` 直接读取用户指针，但未开启 `SUM`

在 RISC-V 中，S-mode 默认不能直接访问带 `PTE_U=1` 的用户页，除非临时设置
`sstatus.SUM`。

当前用户代码页和栈页都用用户权限映射：

- 代码页：`PTE_USER_RX`
- 栈页：`PTE_USER_RW`

因此 `write(1, "Hello CuteOS!\n", 14)` 进入内核后，`sys_write()` 里的这段代码：

```c
const char *s = (const char *)buf;
for (uint64_t i = 0; i < len; i++)
    uart_putc(s[i]);
```

如果不打开 `SUM`，后续就会在 S-mode 访问用户地址时再次异常。

修复后的做法是：

```c
bool had_sum = user_access_begin();
...
user_access_end(had_sum);
```

其中 `user_access_begin()` 会在必要时设置 `SSTATUS_SUM`，而 `user_access_end()`
在退出前恢复原状态。

这一步不是导致“最初那个非法指令 panic”的直接原因，但它是用户态 `write()`
真正跑通的必要条件。

### 5.5 根因五：用户代码页复制后缺少 `fence.i`

`exec_user_binary()` 会先把 `init.bin` 复制到新分配的用户代码页：

```c
memcpy(code_page, bin_start, bin_size);
```

随后 CPU 就会从这页上开始取指。

按照 RISC-V 规则，数据写入一块即将作为指令执行的内存后，应执行 `fence.i`
确保后续取指看到最新内容。因此修复中补上了：

```c
__asm__ volatile("fence.i" : : : "memory");
```

这不是本次第二次现场 (`Instruction Page Fault`) 的直接原因，但它是这条路径的
必要正确性保证，应该和本次修复一起落地。

---

## 6. 修复内容

### 6.1 `kernel/exec.c`

修复点：

1. 把 `tf->sstatus` 从 `SSTATUS_SPP | SSTATUS_SPIE` 改为 `SSTATUS_SPIE`。
2. 在复制用户 binary 后执行 `fence.i`。
3. 不再把 `trap_frame` 放在固定的“内核栈顶”。
4. 改为从当前 `sp` 往下预留 scratch slot 和 `trap_frame`。
5. 构造 `trap_frame` 后不再发生任何 C 函数调用，直接进入内联汇编切换。

当前关键位置：

- `kernel/exec.c:81`：`memcpy()` 后执行 `fence.i`
- `kernel/exec.c:114`：读取当前 `sp`
- `kernel/exec.c:119`：预留 scratch slot
- `kernel/exec.c:124`：设置 `sepc = 0x10000`
- `kernel/exec.c:126`：设置 `SPP=0`
- `kernel/exec.c:137`：`satp -> sscratch -> sp -> __trapret`

### 6.2 `syscall/syscall.c`

修复点：

1. 增加 `user_access_begin()` / `user_access_end()`。
2. 在 `sys_write()` 读取用户缓冲区前临时打开 `SUM`。

当前关键位置：

- `syscall/syscall.c:34`：`user_access_begin()`
- `syscall/syscall.c:44`：`user_access_end()`
- `syscall/syscall.c:63`：`sys_write()` 中启用 `SUM`

### 6.3 `include/asm/csr.h`

修复点：

1. 增加 `SSTATUS_SUM` 位定义。

当前关键位置：

- `include/asm/csr.h:41`

---

## 7. 为什么最初会表现成“非法指令”，而不是更直观的页错误

这是本次问题里最容易让人误判的一点。

最开始的现场并不是：

```text
origin=user, sepc=0x10000 附近
```

而是：

```text
origin=kernel, sepc=高地址, scause=illegal instruction
```

这会让人第一反应怀疑：

- 用户 binary 本身坏了
- `init.bin` 链接地址不对
- `__trapret` 汇编写错了

但真实情况是两个问题叠加：

1. `SPP=1` 把返回特权级伪装成了“内核态返回”。
2. `trap_frame` 被覆盖又把返回地址伪装成了“随机高地址”。

于是 CPU 看起来像是在内核里执行了一条坏指令，实际上是“错误的返回现场”导致的。

这也是为什么在修掉 `SPP` 之后，现象马上变成了更具信息量的：

```text
origin=user scause=0xc sepc=0xffffffc08049bf50
```

一旦变成这个样子，问题就开始明显指向“用户态 `PC` 恢复错了”。

---

## 8. 修复后的验证结果

使用如下命令验证：

```bash
make -j4 BUILD=debug SANITIZE=none
timeout 12s make qemu BUILD=debug SANITIZE=none
```

修复后的关键输出：

```text
init running (PID 1)
exec: loading user binary (332 bytes)
exec: switching to user mode (sepc=0x0000000000010000, sp=0x0000000080000000, pgd=0x0000000080499000)
Hello CuteOS!

KERNEL PANIC: user exit with code 0
```

这说明：

1. `exec_user_binary()` 已能正确创建并切换到用户页表。
2. `__trapret` / `sret` 已能正确返回 U-mode。
3. 用户 `_start`、`main()`、`write()` 已能执行。
4. `EXC_ECALL_U -> do_syscall() -> sys_write()` 已能工作。
5. 当前唯一剩余 panic 来自 `SYS_exit` 的占位实现，属于设计内行为。

---

## 9. 结论

本次问题的最终结论如下：

### 9.1 方案本身没有缺大块

从文件布局和主流程角度看，方案已经基本完整落地。出错点不在
“没有实现某个模块”，而在“已经实现的切换细节是否严格满足 trap/栈/特权级约束”。

### 9.2 最初 panic 的直接根因是“错误返回现场”

真正把系统带偏的是：

- `SPP=1`
- `trap_frame` 放在仍在使用的 C 栈顶部

这两个问题叠加后，导致 `__trapret` 恢复出错误的 `PC/SP/sstatus`，
最终表现为 `origin=kernel scause=0x2` 的非法指令异常。

### 9.3 用户态链路现已打通

修复后，CuteOS 已经能够完成：

```text
kernel thread -> exec_user_binary -> user _start -> main -> write -> ecall
```

这意味着方案的核心目标已经实现。

### 9.4 当前剩余行为是预期中的占位实现

最后的：

```text
KERNEL PANIC: user exit with code 0
```

不是 bug，而是当前 `SYS_exit` 的设计选择。后续如果进入下一阶段，应将其替换为：

- 正常回收当前任务
- 或切回调度器
- 或在单用户 bring-up 阶段直接关机 / 停机

---

## 10. 后续建议

1. 为 `exec_user_binary()` 增加更明确的注释，强调 `trap_frame` 不能放在活跃 C 栈顶部。
2. 后续引入统一 `copy_from_user()` / `copy_to_user()`，不要在系统调用里直接裸读用户指针。
3. 为 `SYS_exit` 实现真正的退出路径，避免把“用户程序正常结束”也表现成 panic。
4. 增加一个专门的用户态 smoke test，验证：
   - 进入 U-mode
   - `SYS_write`
   - `SYS_exit`
   - 用户栈顶是否正确
5. 如果后续开始支持更多用户程序加载方式（ELF、文件系统加载），保留本报告中的
   “scratch slot + trap_frame 放置约束” 作为通用设计规则。
