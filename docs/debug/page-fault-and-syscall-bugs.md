# CuteOS 缺页处理与基础 Syscall 调试报告

本文记录 Stage 3.5（缺页处理）和 3.6（基础 syscall）实现过程中遇到的
四个 bug。这四个问题层层叠加，从"用户程序 exit 后内核崩溃"到"brk 扩展
后堆内存访问 page fault"，逐个暴露了调度器、链表操作、地址空间常量和
VMA 查找逻辑中的边界缺陷。

四个 bug 按发现顺序排列：

1. `schedule()` 在 runqueue 为空且当前进程不可运行时直接返回，导致 ZOMBIE
   进程继续执行并在 `sret` 路径上崩溃。
2. `do_exit()` 调用 `sched_dequeue()` 时对已出队的链表节点再次 `list_del()`，
   触发 NULL 指针解引用。
3. `TASK_SIZE`（0x40000000）与用户栈实际位置（0x7FFFF000）不一致，导致
   `access_ok()` 拒绝所有栈上的 buffer，`write()` 系统调用静默失败。
4. `mm_brk()` 的 VMA 查找条件 `vm_start >= mm->brk` 错误匹配栈 VMA，
   破坏了栈 VMA 的 vm_end，使堆内存访问无法找到合法 VMA。

---

## 1. 初始错误现象

实现 `sys_exit` 后首次运行 QEMU，用户程序打印 "Hello CuteOS!" 后内核
进入无限循环的 page fault：

```text
Hello CuteOS!
do_exit: pid=1 exit_code=0
page fault: illegal access (no VMA) type=store addr=0x0000000000000000 sepc=0xffffffc080202974 origin=kernel pid=1
do_exit: pid=1 exit_code=1
page fault: illegal access (no VMA) type=store addr=0x0000000000000000 sepc=0xffffffc080202974 origin=kernel pid=1
do_exit: pid=1 exit_code=1
page fault: illegal access (no VMA) type=store addr=0x0000000000000000 sepc=0xffffffc080202974 origin=kernel pid=1
...
KERNEL PANIC: BUG: kernel/task.c:66 canary != CANARY_MAGIC
```

关键观察：

1. `do_exit: pid=1 exit_code=0` 说明 `sys_exit` 正确调用了 `do_exit(0)`。
2. 但随后出现 store page fault at addr=0x0 in kernel mode with pid=1。
3. 此循环无限重复，直到 canary 检测到栈破坏后 panic。

---

## 2. 根因分析

### 2.1 Bug 1：`schedule()` runqueue 为空时未切换到 idle

#### 期望行为

当用户进程调用 `exit(0)` 时，执行路径为：

```text
ecall → trap_handler → do_syscall → sys_exit → do_exit(0)
  → current->state = TASK_ZOMBIE
  → schedule()
  → switch_to idle
  → idle 永远运行（runqueue 为空）
```

`schedule()` 永不返回，`do_exit` 之后的代码（包括 `unreachable()`）不可达。

#### 实际行为

初始版本的 `schedule()` 在 runqueue 为空时直接 `return`：

```c
void schedule(void)
{
    ...
    if (list_empty(&runqueue))
        return;    // ← BUG: 如果 current 是 ZOMBIE，不应该返回
    ...
}
```

系统中只有两个任务：idle（PID 0）和 init（PID 1）。PID 1 正在运行，
runqueue 为空。当 PID 1 调用 `do_exit` → `schedule()`：

1. `schedule()` 发现 runqueue 为空，直接返回。
2. 执行回到 `do_exit`，继续执行 `unreachable()`。
3. `unreachable()` 是 `__builtin_unreachable()`，编译器可能优化掉后续代码，
   也可能直接 fall through。
4. 函数返回到调用链 `sys_exit → do_syscall → trap_handler → __trapret`。
5. `__trapret` 恢复 trap_frame 并 `sret` 回用户态——但进程已经 ZOMBIE！

#### 崩溃机制

回到用户态后，CPU 执行用户程序的后续指令（可能是 exit ecall 之后的垃圾），
或者因为 sscratch 仍然指向 PID 1 的内核栈，下次 trap 时走 U→S 路径，
最终在内核态解引用了无效地址。

#### 修复

在 `schedule()` 中增加判断：runqueue 为空且当前进程不可运行时，强制切换到 idle：

```c
if (list_empty(&runqueue)) {
    if (current == &idle_task ||
        current->state == TASK_RUNNING)
        return;   /* idle 或可运行进程继续执行 */

    /* 当前进程不可运行（ZOMBIE/SLEEPING），切到 idle */
    struct task_struct *prev = current;
    check_canary(prev);
    current = &idle_task;
    switch_to(&prev->ctx, &idle_task.ctx);
    return;
}
```

关键设计：`TASK_RUNNING` 的进程在 runqueue 为空时应该继续运行（yield 语义），
只有 `TASK_ZOMBIE` / `TASK_SLEEPING` 的进程才需要强制切换。

修改位置：`kernel/sched.c`。

---

### 2.2 Bug 2：`do_exit()` 的 `sched_dequeue` NULL 指针崩溃

#### 修复 Bug 1 后的新现象

修复 Bug 1 后运行，QEMU 输出：

```text
do_exit: pid=1 exit_code=0
page fault: illegal access (no VMA) type=store addr=0x0000000000000000 sepc=0xffffffc080202974 origin=kernel pid=1
do_exit: pid=1 exit_code=1
```

通过 objdump 反汇编确认 `sepc=0xffffffc080202974` 位于 `sched_dequeue`
函数中，故障指令为：

```asm
sched_dequeue:
    ...
    e398   sd a4, 0(a5)    # a5 = NULL → store page fault at addr=0x0
```

#### 根因

`do_exit` 的初始实现包含这段代码：

```c
if (!list_empty(&current->run_list))
    sched_dequeue(current);
```

意图是"如果进程还在 runqueue 中就移除它"。但运行中的进程**不在 runqueue 中**
——`schedule()` 取出进程时已经调用了 `list_del()`。

`list_del()` 的实现（`include/kernel/list.h`）：

```c
static inline void list_del(struct list_head *entry)
{
    __list_del(entry->prev, entry->next);
    entry->prev = nullptr;    // ← 设置为 NULL
    entry->next = nullptr;    // ← 设置为 NULL
}
```

出队后，`run_list.next = NULL, run_list.prev = NULL`。

`list_empty()` 的实现：

```c
static inline bool list_empty(const struct list_head *head)
{
    return head->next == head;    // NULL == &run_list → false!
}
```

由于 `NULL != &current->run_list`（一个是 0，另一个是结构体成员地址），
`list_empty()` 返回 false。于是 `sched_dequeue` 被调用，对已经被
`list_del()` 清零的节点再次调用 `list_del(NULL, NULL)`：

```c
static inline void __list_del(struct list_head *prev, struct list_head *next)
{
    next->prev = prev;    // NULL->prev = NULL → 解引用 NULL → page fault!
    prev->next = next;
}
```

#### 完整崩溃链

```text
do_exit()
  → list_empty(&current->run_list) → false (NULL != self)
  → sched_dequeue(current)
    → list_del(&current->run_list)
      → __list_del(NULL, NULL)
        → NULL->prev = ... → store page fault at addr=0x0
```

这个 page fault 又被 `do_page_fault` 捕获，检测到地址 0x0 不在任何 VMA 中，
再次调用 `do_exit(1)`，形成无限循环。每次循环都写脏 PID 1 的内核栈，
最终触发 canary 检测。

#### 修复

移除 `do_exit` 中的 `sched_dequeue` 调用。原因：

1. 运行中的进程已被 `schedule()` 从 runqueue 中取出。
2. `schedule()` 在 re-enqueue 时检查 `state == TASK_RUNNING`，
   ZOMBIE 进程不会被重新入队。
3. 因此 `do_exit` 不需要 dequeue。

```c
void do_exit(int code)
{
    current->state = TASK_ZOMBIE;
    /* 运行中的进程不在 runqueue 中，无需 sched_dequeue */
    schedule();
    unreachable();
}
```

修改位置：`kernel/exit.c`。

---

### 2.3 Bug 3：`TASK_SIZE` 与用户栈位置不一致

#### 修复 Bug 1/2 后的新现象

系统不再崩溃，用户程序运行并通过 `sys_exit` 干净退出。但 `getpid` 和 `brk`
的返回值无法通过 `write()` 输出：

```text
[TEST] getpid =  (expected 1)
[TEST] brk(0) = 
```

`print_hex` 函数在栈上分配 `char buf[19]`，然后调用 `write(1, buf, 18)`。
输出为空，说明 `write` 系统调用静默失败了。

#### 根因

`include/asm/page.h` 定义：

```c
#define TASK_SIZE  0x40000000    /* 1 GiB */
```

但 `kernel/exec.c` 将用户栈放在：

```c
#define USER_STACK_TOP   0x80000000UL
#define USER_STACK_BASE  0x7FFFF000UL
```

`mm/uaccess.c` 的 `access_ok()` 检查：

```c
bool access_ok(const void *addr, size_t size)
{
    uintptr_t a = (uintptr_t)addr;
    if (a + size > TASK_SIZE)    // 栈地址 0x7FFFFxxx > 0x40000000 → false
        return false;
    return true;
}
```

`sys_write()` 调用 `access_ok(buf, len)` 检查用户 buffer：

```c
if (!access_ok(buf, len))
    return -EFAULT;
```

栈上 buffer 地址约为 `0x7FFFFxxx`，远超 `TASK_SIZE`（`0x40000000`），
`access_ok` 返回 false，`sys_write` 直接返回 `-EFAULT`。

但 `write()` 的返回值被忽略了（用户态没有检查），所以现象是"静默失败"。

字符串字面量（如 `"Hello CuteOS!\n"`）不受影响，因为它们存储在代码段
（`0x10000` 区域），低于 `TASK_SIZE`。

#### 为什么之前没暴露

之前的用户程序（`user/init/init.c`）只做一件事：

```c
write(1, "Hello CuteOS!\n", 14);
return 0;
```

`"Hello CuteOS!\n"` 是代码段中的字符串字面量，地址约 `0x101xx`，
低于 `0x40000000`，`access_ok` 通过。没有任何栈上的 buffer 传给 `write()`。

新增的 `print_hex` 函数首次将栈上 buffer 传给 `write()`，才触发了这个 bug。

#### 修复

将 `TASK_SIZE` 更新为覆盖完整的用户地址空间（包含栈）：

```c
#define TASK_SIZE  0x80000000UL
```

修改位置：`include/asm/page.h`。

---

### 2.4 Bug 4：`mm_brk()` VMA 查找误匹配栈 VMA

#### 修复 Bug 3 后的新现象

`print_hex` 输出正常，可以看到 `brk(0) = 0x11000` 和
`brk(0x12000) = 0x12000`。但访问堆内存时仍然 page fault：

```text
[TEST] brk(0x0000000000012000) = 0x0000000000012000
page fault: illegal access (no VMA) type=store addr=0x0000000000011000 sepc=... origin=user pid=1
```

地址 `0x11000` 应该在堆 VMA（`0x11000`~`0x12000`）范围内，
但 `find_vma` 返回 NULL。

#### 根因

`mm_brk()` 中的 VMA 查找逻辑（修复前）：

```c
for (int i = 0; i < NR_VMA; i++) {
    if (mm->vma[i].used &&
        mm->vma[i].vm_start >= mm->brk) {
        heap_vma = &mm->vma[i];
        break;
    }
}
```

执行 `exec_user_elf` 后的 VMA 布局：

| VMA | vm_start | vm_end | vm_flags |
|-----|----------|--------|----------|
| 代码段 | 0x10000 | 0x10144 | VM_READ\|VM_EXEC |
| 用户栈 | 0x7FFFF000 | 0x80000000 | VM_READ\|VM_WRITE |

调用 `brk(0x12000)` 时 `mm->brk = 0x11000`：

1. 代码段 VMA：`vm_start(0x10000) >= brk(0x11000)` → false → 跳过
2. 栈 VMA：`vm_start(0x7FFFF000) >= brk(0x11000)` → **true** → 匹配！

`heap_vma` 被错误地指向了栈 VMA。后续代码：

```c
if (!heap_vma) {
    /* 首次扩展：创建堆 VMA */    ← 不执行
} else {
    heap_vma->vm_end = addr;    ← 栈 VMA 的 vm_end 被改为 0x12000!
}
```

栈 VMA 从 `[0x7FFFF000, 0x80000000)` 被破坏为 `[0x7FFFF000, 0x00012000)`。
`vm_end < vm_start`，这个 VMA 已经完全无效。

同时，堆区域 `[0x11000, 0x12000)` 从未创建 VMA。
`find_vma(mm, 0x11000)` 遍历所有 VMA：

1. 代码段：`0x10000 <= 0x11000 < 0x10144` → `0x11000 < 0x10144` 为 false
2. 栈（已破坏）：`0x7FFFF000 <= 0x11000` → false
3. 无匹配 → 返回 NULL → page fault 判定为非法访问

#### 修复

将 VMA 查找条件改为"查找覆盖 brk 的 VMA"：

```c
uintptr_t old_brk = mm->brk;
for (int i = 0; i < NR_VMA; i++) {
    if (mm->vma[i].used &&
        mm->vma[i].vm_start <= old_brk &&
        mm->vma[i].vm_end >= old_brk) {
        heap_vma = &mm->vma[i];
        break;
    }
}
```

执行 `brk(0x12000)` 时 `old_brk = 0x11000`：

1. 代码段：`0x10000 <= 0x11000 && 0x10144 >= 0x11000` → false
2. 栈：`0x7FFFF000 <= 0x11000` → false
3. 无匹配 → `heap_vma = NULL` → 创建新的堆 VMA `[0x11000, 0x12000)` ✅

后续 `brk(0x13000)` 时 `old_brk = 0x12000`：

1. 代码段：不覆盖 0x12000 → skip
2. 栈：不覆盖 0x12000 → skip
3. 堆 VMA `[0x11000, 0x12000)`：`0x11000 <= 0x12000 && 0x12000 >= 0x12000` → true
4. 扩展为 `[0x11000, 0x13000)` ✅

修改位置：`mm/mmap.c`。

---

## 3. 修复后验证

修复全部四个 bug 后，QEMU 输出：

```text
=== CuteOS Syscall Test ===
[TEST] getpid = 0x0000000000000001 (expected 1)        ✅ getpid
[TEST] write: OK                                        ✅ write
[TEST] yield...                                         ✅ yield 调用
[TEST] yield: returned OK                               ✅ yield 返回
[TEST] brk(0) = 0x0000000000011000                      ✅ brk 查询
[TEST] brk(0x0000000000012000) = 0x0000000000012000     ✅ brk 扩展
[TEST] heap[0] = 0x42, heap[100] = 0x43                 ✅ lazy page fault
[TEST] second yield: OK                                 ✅ 再次 yield
=== All tests passed ===                                ✅
do_exit: pid=1 exit_code=0
```

端到端验证了以下完整链路：

```text
用户 brk(0x12000)
  → sys_brk → mm_brk (lazy: 只创建 VMA，不分配物理页)
  → 用户写入 heap[0] = 0x42
  → MMU: 0x11000 无映射 → store page fault (scause=15)
  → __alltraps → trap_handler
  → do_page_fault
    → find_vma(mm, 0x11000) → 找到堆 VMA [0x11000, 0x12000)
    → check_vma_permission(store, VM_READ|VM_WRITE) → true
    → get_free_page(0) → 分配物理页
    → memset(page, 0, 4096) → 清零
    → map_page(mm->pgd, 0x11000, pa, PTE_USER_RW)
    → sfence_vma_addr(0x11000)
  → sret → 重新执行 store 指令
  → 写入成功 → heap[0] = 0x42 ✅
```

---

## 4. Bug 依赖关系

四个 bug 按发现顺序排列，前一个的修复是暴露后一个的前提：

```text
Bug 1 (schedule 不切换 idle)
  └→ 修复后暴露 Bug 2 (do_exit sched_dequeue NULL)
       └→ 修复后 yield/exit 工作，暴露 Bug 3 (TASK_SIZE 不匹配)
            └→ 修复后 print_hex 工作，暴露 Bug 4 (brk VMA 查找)
```

---

## 5. 经验教训

### 5.1 链表节点的"已删除"状态需要特殊处理

Linux 内核的 `list_del()` 用 `LIST_POISON1/LIST_POISON2`（非 NULL 的
非法地址）标记已删除节点，这样再次 `list_del()` 会触发明显的中断而非
静默的 NULL 解引用。当前 cuteOS 使用 `nullptr`，`list_empty()` 无法
区分"节点不在任何链表中"和"节点在空链表中"。

建议：引入 `LIST_POISON` 机制，或在 `sched_dequeue` 中使用
`list_del_init()` 使节点回到自循环状态。

### 5.2 `TASK_SIZE` 必须与实际用户地址空间布局一致

`TASK_SIZE` 是用户地址空间的硬上限，影响 `access_ok`、`brk` 上限检查等
所有用户/内核边界检查。它必须覆盖所有可能的用户地址，包括栈。

建议：在 `exec.c` 中用 `USER_STACK_TOP` 作为 TASK_SIZE 的来源，
或将两者统一到一个 `include/asm/page.h` 的定义中。

### 5.3 VMA 查找需要精确的匹配条件

`vm_start >= mm->brk` 这样的松散条件在高地址有其他 VMA 时会误匹配。
正确的做法是查找"覆盖目标地址的 VMA"（`vm_start <= addr && vm_end > addr`），
或为不同用途的 VMA 打标签（堆/栈/代码）。

### 5.4 `schedule()` 必须处理"无其他可运行任务"的边界

当系统中只有一个用户进程时，`yield()` 和 `exit()` 都会遇到 runqueue 为空
的情况。`schedule()` 需要区分：

- 当前进程可运行（yield）：继续执行当前进程
- 当前进程不可运行（exit/sleep）：切换到 idle

---

## 6. 修改文件清单

| 文件 | Bug | 修改内容 |
|------|-----|----------|
| `kernel/sched.c` | #1 | runqueue 为空时检查 `current->state`，ZOMBIE 进程切换到 idle |
| `kernel/exit.c` | #2 | 移除 `sched_dequeue` 调用，运行中进程不在 runqueue |
| `include/asm/page.h` | #3 | `TASK_SIZE` 从 `0x40000000` 改为 `0x80000000` |
| `mm/mmap.c` | #4 | VMA 查找从 `vm_start >= brk` 改为 `vm_start <= old_brk && vm_end >= old_brk` |
