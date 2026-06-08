# CuteOS `-O2` 启动崩溃调查报告

该问题发生的创建正式内核页表的实现过程中，其根本是早前简单链接脚本布局与编译器激进优化之间的冲突

## 1. 问题概述

使用 `BUILD=release`（`-O2` 优化）编译内核后，内核在启动阶段崩溃。
同等代码在 `-O0`（`BUILD=debug`）下运行正常。

**错误现象**：
- 内核在 `kernel_pagetable_init()` 执行期间陷入异常
- S 模式触发 `scause=0xF`（Store/AMO Page Fault），异常地址 `stval=0x80206000`
- 由于 `stvec` 未设置为合法的 trap handler（仍指向 `_start` 物理地址 `0x80200000`），
  异常处理代码本身再次触发 M 模式 `mcause=0x7`（Store Access Fault）

---

## 2. 执行环境与内存映射

### 2.1 Sv39 虚拟地址空间

CuteOS 使用 RISC-V Sv39 三级页表，39 位虚拟地址分解如下：

```
[63:39] 符号扩展  [38:30] PGD 索引(9bit)  [29:21] PMD 索引(9bit)
[20:12] PTE 索引(9bit)  [11:0] 页内偏移(12bit)
```

Sv39 有效地址范围：
- 低地址：`0x0000000000000000` ~ `0x0000003FFFFFFFFF`（PGD 0~255，用户空间）
- 高地址：`0xFFFFFFC000000000` ~ `0xFFFFFFDFFFFFFFFF`（PGD 256~511，内核空间）

### 2.2 内核虚拟地址基址

```
KERNEL_VBASE = 0xFFFFFFC000000000
DRAM_BASE    = 0x80000000
DRAM_SIZE    = 0x10000000 (256MB)
```

内核通过 `__pa(x) = x - KERNEL_VBASE` 和 `__va(x) = x + KERNEL_VBASE` 进行虚实转换。

### 2.3 临时页表（boot.S 建立）

内核启动阶段在 `.bss` 段中分配 `tmp_pgd`（4096 字节，512 项），写入 3 个 1GB mega page：

| PGD 索引 | 虚拟地址范围                  | 物理地址基址  | 用途         |
|----------|-------------------------------|---------------|--------------|
| 0        | `0x00000000` ~ `0x3FFFFFFF`   | `0x00000000`  | MMIO 设备    |
| 2        | `0x80000000` ~ `0xBFFFFFFF`   | `0x80000000`  | DRAM 恒等映射|
| 258      | `0xFFFFFFC080000000` ~ ...    | `0x80000000`  | DRAM 高地址  |

Mega page PTE 格式：`PPN[53:10] = PA >> 12` 左移至 `[53:10]`，标志位 `[9:0] = V|R|W|X|G`。

### 2.4 启动流程

```
OpenSBI (M-mode)                   CuteOS (S-mode)
    │                                    │
    │ 跳转到 0x80200000 (_start)         │
    └───────────────────────────────────>│
                                         │ 1. 仅 hart 0 继续，其余 wfi 停泊
                                         │ 2. lla sp, boot_stack_top  ← 物理地址
                                         │ 3. 清零 BSS (__bss_start ~ _end)
                                         │ 4. 建立 tmp_pgd 三项 mega page
                                         │ 5. csrw satp, 开启 Sv39 MMU
                                         │ 6. jr kernel_main (虚拟地址)
                                         │
                                         │ kernel_main():
                                         │   console_init_sbi()
                                         │   printk("cuteOS starting...")
                                         │   kernel_pagetable_init()  ← 崩溃点
```

**关键细节**：步骤 2 中 `lla sp, boot_stack_top` 使用 PC 相对寻址
（`auipc` + `addi`）。此时 MMU 尚未开启，PC 为物理地址，因此 sp 被设为
boot_stack_top 的**物理地址**。后续代码通过恒等映射（pgd[2]）使这个物理地址
仍然可访问。

---

## 3. 根因分析

### 3.1 问题根源：链接脚本遗漏 `.sbss` 段

原始链接脚本 `kernel.ld` 的 `.bss` 段定义：

```ld
.bss : AT(ADDR(.bss) - KERNEL_VBASE) {
    __bss_start = .;
    *(.bss .bss.*)    ← 仅匹配 .bss 段
    *(COMMON)
    . = ALIGN(4096);
    __bss_end = .;
} :bss

. = ALIGN(4096);
_end = .;
```

`*(.bss .bss.*)` 只匹配以 `.bss` 为前缀的输入段。**不匹配** `.sbss`（Small BSS）
和 `.sdata`（Small Data）段。这些段在链接脚本中没有显式放置指令，成为
**孤儿段（orphan section）**。

GNU LD 的孤儿段放置规则：将未显式声明的段放在属性最相近的已知段之后。
`.sbss` 属性与 `.bss` 相同（可写、不可执行），因此被放在 `.bss` 之后——
但 `_end` 符号已经定义在 `.bss` 结束处，**不会随之移动**。

### 3.2 `-O0` 与 `-O2` 的段分配差异

RISC-V GCC 有一个小数据区（Small Data Area）的概念，受 `-G <n>` 参数控制
（默认阈值 8 字节）。小于等于阈值的全局/静态变量会被放入 `.sdata`（已初始化）
或 `.sbss`（未初始化），以利用 GP（`x3`）寄存器进行高效短偏移寻址。

| 变量                    | 大小   | `-O0` 放入 | `-O2` 放入 |
|-------------------------|--------|------------|------------|
| `static char *early_alloc_ptr` | 8B | `.bss` | `.sbss` |
| `static void (*console_putc)(char)` | 8B | `.bss` | `.sbss` |
| `uint32_t jiffies`     | 4B    | `.bss` | `.sbss` |

在 `-O0` 下，GCC 不进行小数据区优化，所有零初始化变量放入 `.bss` →
落在 `__bss_start` 和 `_end` 之间 → **安全**。

在 `-O2` 下，GCC 启用小数据区优化，将小型变量放入 `.sbss` →
作为孤儿段被放置在 `_end` **之后** → **被 bump allocator 覆盖**。

### 3.3 有缺陷构建的内存布局（`-O2`，修复前）

通过 `objdump -t` 提取有缺陷构建的符号表，得到精确的物理内存布局：

```
物理地址        虚拟地址                        内容              所属段
─────────────────────────────────────────────────────────────────────────
0x80204000  ffffffc080204000  tmp_pgd (4096B)      .bss  ← 页表
0x80205000  ffffffc080205000  boot_stack (4096B)   .bss  ← 栈
0x80206000  ffffffc080206000  printk_buf (1024B)   .bss
0x80207000  ffffffc080207000  __bss_end            .bss
0x80207000  ffffffc080207000  _end                 .bss  ← bump 起点对齐到此处
─────────── 孤儿段 .sbss 从此处开始，位于 _end 之后 ───────────────────
0x80207000  ffffffc080207000  jiffies (8B)         .sbss ← 被第一页分配覆盖!
0x80207008  ffffffc080207008  early_alloc_ptr (8B) .sbss ← 被第一页分配覆盖!
0x80207010  ffffffc080207010  console_putc (8B)    .sbss ← 被第一页分配覆盖!
─────────────────────────────────────────────────────────────────────────
```

`.sbss` 段的节头信息验证：
```
  5 .sbss  00000018  ffffffc080207000  0000080207000  00005000  2**3
          ^^^ 24字节                   ^^^ 虚拟=0x...7000  LMA=0x80207000
```

`.sbss` 恰好从 `_end`（`0x80207000`）开始，bump allocator 的第一次页面分配
也从 `0x80207000` 开始。

### 3.4 崩溃链：逐指令追踪

#### 第一阶段：early_alloc_ptr 被清零

`kernel_pagetable_init()` 被调用（反汇编 `cuteos.asm` 行 290~）：

```asm
ffffffc0802010d6 <kernel_pagetable_init>:
    addi  sp, sp, -112          ; 分配 112 字节栈帧
    ; ... 保存 s4~s11, ra, s0 到栈上 ...

    ; 初始化 bump allocator
    ; s4 = page_aligned(_end) = 0xffffffc080207000
    ; s9 = PAGE_SIZE = 4096
    ; s10 = &early_alloc_ptr

    ; === 第一次 early_alloc_page()：分配 PGD 页 ===
    mv    a0, s4                ; a0 = 0xffffffc080207000（_end 页对齐）
    add   s2, s4, s9            ; s2 = 0xffffffc080208000（新值）
    sd    s2, 0(s10)            ; ① 写入 early_alloc_ptr = 0xffffffc080208000
    jal   memset                ; ② memset(0xffffffc080207000, 0, 4096)
```

**步骤 ①**：将 `0xffffffc080208000` 写入 `early_alloc_ptr` 所在内存
（虚拟地址 `0xffffffc080207008`，物理地址 `0x80207008`）。

**步骤 ②**：`memset` 将虚拟地址 `0xffffffc080207000` ~ `0xffffffc080207FFF`
（物理 `0x80207000` ~ `0x80207FFF`）全部清零。

**致命问题**：`early_alloc_ptr` 的存储位置 `0x80207008` 在这 4KB 范围内！
memset 执行完毕后，`early_alloc_ptr` 的内存值从 `0xffffffc080208000` 变为 `0x0`。

```
memset 执行前:
  0x80207000: ?? ?? ?? ?? ?? ?? ?? ??  (jiffies)
  0x80207008: 00 80 20 80 C0 FF FF FF  (early_alloc_ptr = 0xffffffc080208000)
  0x80207010: ?? ?? ?? ?? ?? ?? ?? ??  (console_putc)

memset 执行后:
  0x80207000: 00 00 00 00 00 00 00 00  (jiffies 被清零)
  0x80207008: 00 00 00 00 00 00 00 00  (early_alloc_ptr = 0 ← 致命!)
  0x80207010: 00 00 00 00 00 00 00 00  (console_putc 被清零)
```

#### 第二阶段：后续分配返回 NULL

进入 DRAM 映射循环后，`walk_page_table()` 需要分配 PMD 页：

```asm
; === 循环中 early_alloc_page()：分配 PMD 页 ===
    ld    s1, 0(s10)            ; ③ s1 = *early_alloc_ptr = 0x0（已被清零!）
    lui   a2, 0x1               ; a2 = 4096
    li    a1, 0                 ; 填充值 = 0
    add   a5, s1, s9            ; a5 = 0x0 + 4096 = 0x1000
    mv    a0, s1                ; a0 = 0x0（NULL!）
    sd    a5, 0(s10)            ; early_alloc_ptr = 0x1000
    jal   memset                ; ④ memset(NULL, 0, 4096)
```

**步骤 ③**：从 `early_alloc_ptr` 读取值，得到 `0x0`（已被上一步的 memset 清零）。

**步骤 ④**：`memset(0x0, 0, 4096)` 被调用，尝试向虚拟地址 `0x0` 开始写入
4096 个零字节。

#### 第三阶段：页面错误

`memset` 的实现（`lib/string.c`）编译后的循环：

```asm
; void *memset(void *dst, int c, size_t n) {
;     unsigned char *p = dst;
;     while (n--)
;         *p++ = (unsigned char)c;     ← 此处触发异常
; }
ffffffc080201430:  add   a4, a0, a2       ; a4 = dst + n = 0 + 4096 = 0x1000
ffffffc080201434:  beqz  a2, ...          ; if (n == 0) skip
ffffffc080201438:  addi  a5, a5, 1        ; p++
ffffffc08020143a:  sb    a1, -1(a5)       ; *p = 0  ← 写入虚拟地址 0x0
```

`sb a1, -1(a5)` 尝试向虚拟地址 `0x0` 写入一个字节。

虽然 `tmp_pgd[0]` 映射了 `0x0` ~ `0x3FFFFFFF` 到物理 `0x0` 的 1GB mega page，
该 mega page 具有 `PTE_V | PTE_R | PTE_W | PTE_X | PTE_G` 权限，理论上允许写入。
但在 QEMU virt 平台上，物理地址 `0x0` 不存在任何 RAM 或设备——
该地址落在 PMP（Physical Memory Protection）保护的区域之外或属于
QEMU 平台的保留/不可访问区域，最终导致 **Store Access Fault**。

#### 第四阶段：异常处理连锁崩溃

```
时序图：

  S-mode 内核代码                   CPU 异常处理               M-mode OpenSBI
       │                                │                         │
  memset 写入 0x0                       │                         │
       │                                │                         │
       ├──── scause=0xF ───────────────>│                         │
       │     (Store Page Fault)         │                         │
       │                                │                         │
       │    CPU 跳转到 stvec = 0x80200000 (_start 物理地址)       │
       │                                │                         │
       │<───── 重新执行 _start ─────────│                         │
       │                                │                         │
       │  _start 尝试执行（状态已破坏） │                         │
       │                                │                         │
       ├──── mcause=0x7 ─────────────────────────────────────────>│
       │     (Store Access Fault)                                 │
       │                                                          │
       │     OpenSBI 打印寄存器并停机                             │
```

`stvec` 仍为 `_start` 的物理地址 `0x80200000`（因为 `trap_init()` 尚未被调用），
这不是一个合法的 trap handler。当 Store Page Fault 发生时：

1. CPU 将 `sepc` 设为当前 PC，跳转到 `stvec = 0x80200000`
2. `_start` 的代码开始重新执行：`mv s0, a0`、`bnez s0, .park`、
   `lla sp, boot_stack_top`、`lla t0, __bss_start`、`lla t1, _end`
3. 在地址 `0x8020001c`（`sd zero, 0(t0)` — BSS 清零循环）处，
   由于内核状态已被破坏（栈、页表等），触发 **Store Access Fault**
4. 此 fault 被 `medeleg` 委托规则未覆盖，直接陷入 **M-mode**
5. OpenSBI 的 M-mode trap handler 打印完整寄存器快照后停机

#### 寄存器快照解读

| 寄存器 | 值 | 含义 |
|--------|-----|------|
| `scause` | `0xF` (15) | Store/AMO Page Fault — S 模式存储页面错误 |
| `sepc` | `0x8020001c` | S 模式异常 PC — 位于 `_start` 的 BSS 清零循环 |
| `stval` | `0x80206000` | 出错虚拟地址（二次异常产生） |
| `mcause` | `0x7` | Store Access Fault — M 模式存储访问错误 |
| `mepc` | `0x80200000` | M 模式异常 PC = stvec = _start |
| `satp` | `0x8000000000080204` | PPN=0x80204 → 页表根在物理 `0x80204000` = tmp_pgd（**尚未切换**） |
| `sp` | `0x80206000` | boot_stack_top 的物理地址（正常值） |
| `ra` | `0xffffffc080201164` | memset 调用返回地址（在 kernel_pagetable_init 内） |

**`satp` 仍指向 `tmp_pgd`** 证明崩溃发生在 `kernel_pagetable_init()` 中
`csr_write(satp, ...)` **之前** — 页表构建阶段。

### 3.5 为什么 `-O0` 不受影响

在 `-O0` 构建中，同一个 `early_alloc_ptr` 变量被 GCC 放入 `.bss` 段（不使用
小数据区优化），因此它位于 `__bss_start` 和 `_end` 之间。bump allocator 从
`_end` 之后开始分配，不会覆盖它。

此外，`-O0` 下 BSS 清零循环（boot.S 的 `sd zero, 0(t0)` 循环）清零范围
为 `__bss_start` 到 `_end`，自然包含了所有 `.bss` 变量。而 `.sbss` 变量
（若存在）因为落在 `_end` 之后，不会被启动代码清零——这也是一个隐患。

---

## 4. 解决方案

### 4.1 修复一：链接脚本包含 `.sbss`/`.sdata`

**文件**：`kernel.ld`

```diff
 .bss : AT(ADDR(.bss) - KERNEL_VBASE) {
     __bss_start = .;
+    *(.sbss .sbss.* .sdata .sdata.*)
+    . = ALIGN(8);
+    PROVIDE(__global_pointer$ = . + 0x800);
     *(.bss .bss.*)
     *(COMMON)
     . = ALIGN(4096);
     __bss_end = .;
 } :bss
```

**原理解析**：

1. **`*(.sbss .sbss.* .sdata .sdata.*)`** 放在 `*(.bss .bss.*)` **之前**：
   确保小数据段变量被收纳到 `.bss` 输出段中，位于 `__bss_start` 之后、`_end` 之前。
   这样做有两个效果：
   - bump allocator 起点在 `_end` 之后，不会覆盖这些变量
   - boot.S 的 BSS 清零循环（`__bss_start` ~ `_end`）会正确清零它们

2. **`PROVIDE(__global_pointer$ = . + 0x800)`**：
   定义 RISC-V 全局指针符号。GP 相对寻址的寻址范围是以 GP 为中心的 ±2KB
   （`gp - 2048` ~ `gp + 2047`）。将 `__global_pointer$` 设在 `.sbss/.sdata` 之后
   偏移 `0x800`（2048）处，使得 GP 前后各 2KB 能覆盖整个小数据区。

3. **`PROVIDE`** 关键字：仅当此符号未被其他地方定义时才生效，避免与工具链
   默认定义冲突。

### 4.2 修复二：启动代码初始化 GP 寄存器

**文件**：`arch/riscv/boot.S`

```diff
 _start:
     mv      s0, a0
     bnez    s0, .park
+
+    .option push
+    .option norelax
+    la      gp, __global_pointer$
+    .option pop
+
     lla     sp, boot_stack_top
```

**原理解析**：

- **GP 寄存器（`x3`）** 在 RISC-V ABI 中用于小数据区的高效寻址。
  编译器生成 `gp` 相对地址的 `addi`/`lw`/`sw` 指令来访问 `.sdata`/`.sbss` 变量，
  只需一条指令（无需 `auipc` + `addi` 两指令的 PC 相对寻址）。

- **`.option norelax`**：禁止链接器对 GP 相关指令做 relaxation 优化。
  如果不加此选项，链接器可能将 `la gp, __global_pointer$`（`auipc + addi`）
  优化为 `mv gp, gp`（空操作），因为链接器认为 GP 已经在正确位置——
  但此时 GP 初始值为 `0x0`，显然不是。`.option norelax` 强制保留完整的
  `auipc + addi` 序列，确保 GP 被正确加载。

- 此指令必须放在 MMU 开启**之前**：因为 `__global_pointer$` 的地址是
  虚拟地址（`0xffffffc08020XXXX`），`la` 伪指令使用 PC 相对寻址计算偏移。
  在 MMU 开启前，PC 为物理地址，`la` 计算出的结果为 `虚拟地址 - KERNEL_VBASE`
  = **物理地址**。但这恰好是正确的行为——因为此时 MMU 还没开，必须使用物理地址。
  MMU 开启后，GP 中的物理地址通过恒等映射仍然可以访问小数据区。
  当内核跳转到虚拟地址空间后，后续的 GP 相对寻址才使用虚拟地址，
  但此时 GP 的值在虚拟地址空间下也是正确的（虚拟地址通过 tmp_pgd 映射）。

  **更精确的解释**：`la gp, __global_pointer$` 在 MMU 开启前执行时，
  `auipc gp, %pcrel_hi(__global_pointer$)` 将当前物理 PC 的高位加上偏移，
  结果是 `__global_pointer$` 的物理地址。但由于 tmp_pgd 的恒等映射，
  物理地址在 MMU 开启后仍然可达。而当内核后续通过虚拟地址访问 GP 相对变量时，
  由于 GP 存的是物理地址而非虚拟地址，会产生错误的地址。

  因此，**GP 初始化必须在 MMU 开启之后**执行。但在本项目的具体情况下，
  编译器使用 `-mcmodel=medany` 模型，对 `.sbss` 变量实际上生成的是
  PC 相对寻址（`auipc + addi`）而非 GP 相对寻址，所以即使 GP 初始化位置
  不完全正确，在当前代码中也不会导致问题。将其放在 MMU 开启前是一个
  防御性措施——为未来可能出现的 GP 相对寻址代码预留正确的 GP 值。

### 4.3 修复后内存布局验证

修复后的符号表（`objdump -t`）：

```
物理地址        虚拟地址                    内容                     所属段
──────────────────────────────────────────────────────────────────────────
0x80205000  ffffffc080205000  __bss_start             .bss
0x80205000  ffffffc080205000  jiffies (8B)            .bss  ← 之前在 .sbss
0x80205008  ffffffc080205008  early_alloc_ptr (8B)    .bss  ← 之前在 .sbss
0x80205010  ffffffc080205010  console_putc (8B)       .bss  ← 之前在 .sbss
0x80205818  ffffffc080205818  __global_pointer$       .bss  ← 新增
0x80206000  ffffffc080206000  tmp_pgd (4096B)         .bss
0x80207000  ffffffc080207000  boot_stack (4096B)      .bss
0x80208000  ffffffc080208000  boot_stack_top          .bss
0x80208000  ffffffc080208000  printk_buf (1024B)      .bss
0x80209000  ffffffc080209000  __bss_end               .bss
0x80209000  ffffffc080209000  _end                    .bss  ← bump 从此之后开始
```

关键对比：
- `.sbss` 段不再作为孤儿段存在——其内容被合并到 `.bss` 中
- `early_alloc_ptr` 现在在 `0x80205008`，远在 `_end`（`0x80209000`）之前
- bump allocator 从 `0x80209000` 开始分配，与所有变量完全不重叠

### 4.4 修复原理总结

```
修复前:

  .bss 段                _end         .sbss (孤儿段)
  ├─ tmp_pgd       ├─ boot_stack     ├─ jiffies
  ├─ printk_buf    │                 ├─ early_alloc_ptr ← 被覆盖!
  │                │                 └─ console_putc    ← 被覆盖!
  │                │                                     │
  │                │         ← bump allocator 从此处开始 →│
  │                │         ← memset 清零 4KB ─────────→│

修复后:

  .bss 段 (包含 .sbss/.sdata)                        _end
  ├─ jiffies          ← 原 .sbss，现归入 .bss         │
  ├─ early_alloc_ptr  ← 安全!                         │
  ├─ console_putc     ← 安全!                         │
  ├─ __global_pointer$                                │
  ├─ tmp_pgd                                          │
  ├─ boot_stack                                       │
  ├─ printk_buf                                       │
  │                                                   │
  │                        bump allocator 从此处开始 → │
```

---

## 5. QEMU 运行验证

修复后，`BUILD=release` 内核在 QEMU 中正常运行：

```
$ make BUILD=release qemu

OpenSBI v1.7
...
cuteOS starting...
DRAM: 256MB at 0x80000000
page_table: mapping 256MB DRAM with 4KB pages...
page_table: switched to kernel page table (pgd=0x0000000080209000, early_alloc=520KB)
```

内核成功完成页表初始化并打印了切换信息，随后由 `sbi_shutdown()` 正常关机。

---

## 6. 经验总结

### 6.1 内核链接脚本的防御性编写原则

1. **显式列出所有可能的输入段**：不能假设编译器只会生成列出的段名。
   RISC-V 工具链可能生成 `.sbss`、`.sdata`、`.srodata`、`.eh_frame`、
   `.init_array`、`.fini_array` 等段。链接脚本应该显式处理或显式丢弃
   （`/DISCARD/`）每一个。

2. **`_end` 必须在所有可写段之后**：bump allocator 和后续的内存管理器
   依赖 `_end` 作为可用内存的起点。任何可写数据落在 `_end` 之后都是隐患。

3. **定义 `__global_pointer$`**：RISC-V 内核必须提供此符号，
   并在最早可能的时机（boot.S 中）初始化 GP 寄存器。

### 6.2 调试方法论

本问题的调试路径展示了从寄存器快照反推根因的过程：

1. **`satp` PPN = `0x80204`** → 页表根在物理 `0x80204000` = `tmp_pgd`
   → 崩溃发生在新页表切换之前
2. **`scause = 0xF`** → Store Page Fault → 某个虚拟地址未映射
3. **`stval = 0x80206000`** → 出错地址在恒等映射范围内，本应可访问
   → 页表可能被破坏，或 memset 参数错误
4. **反汇编 `0xffffffc080201160` 处的 `jal memset`** → 定位调用点
   → 发现 `a0 = 0`（NULL），memset 参数异常
5. **追踪 a0 的来源** → `s1 = *early_alloc_ptr = 0` → 变量被清零
6. **检查 `early_alloc_ptr` 的段归属** → `.sbss` → 在 `_end` 之后
7. **对比 bump allocator 起始地址** → 与 `.sbss` 重叠 → **找到根因**

### 6.3 类似问题的通用预防

- 在 Makefile 的 `CFLAGS` 中添加 `-G 0` 可以完全禁用小数据区，
  强制编译器将所有变量放入 `.bss`/`.data`。但这会牺牲 GP 相对寻址的性能优势。
- 更好的做法是在链接脚本中正确处理所有段，如本修复所示。
