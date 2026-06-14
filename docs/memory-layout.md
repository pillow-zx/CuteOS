# CuteOS 内存布局与设计说明

> 本文档描述 CuteOS 当前内存子系统的设计状态与实现细节，涵盖物理内存布局、虚拟地址空间划分、页表结构、物理内存管理层次、用户地址空间管理、缺页处理、进程地址空间复制等方面。

## 1. 概述

CuteOS 是一个基于 **RISC-V 64 位 (RV64)** 的教学操作系统，运行在 QEMU `virt` 机器平台上，使用 **Sv39** 三级页表进行虚拟内存管理。系统可管理 **256 MB** 物理内存（DRAM），内核通过高地址直接映射访问全部物理内存，用户进程拥有独立的低 2 GB 地址空间。

### 1.1 核心地址常量

|       常量      |           值          |           定义位置          |                   说明                 |
|-----------------|-----------------------|-----------------------------|----------------------------------------|
|  `DRAM_BASE`    | `0x80000000`          | `include/asm/page.h:25`     | DRAM 物理起始地址                      |
|  `DRAM_SIZE`    | `0x10000000` (256 MB) | `include/asm/page.h:26`     | DRAM 物理大小                          |
|  `KERNEL_VBASE` | `0xFFFFFFC000000000`  | `include/asm/page.h:32`     | 内核虚拟地址基址                       |
|  `BASE_ADDRESS` | `0x80200000`          | `kernel.ld:4`               | 内核映像物理加载地址                   |
|  `TASK_SIZE`    | `0x80000000`          | `include/asm/page.h:29`     | 用户地址空间上限                       |
|  `USER_STACK_TOP` | `0x80000000`        | `include/asm/page.h`        | 用户栈顶，初始 SP 值                   |
|  `USER_STACK_BASE` | `0x7FFFF000`       | `include/asm/page.h`        | 用户栈底，当前固定 1 页栈              |
|  `SIGNAL_TRAMPOLINE_ADDR` | `0x7FFFE000` | `include/kernel/signal.h` | 信号返回蹦床页固定地址                 |
|  `PAGE_SIZE`    | `4096`                | `include/asm/page.h:14`     | 页大小 (4 KiB)                         |
|  `PAGE_SHIFT`   | `12`                  | `include/asm/page.h:15`     | 页偏移位数                             |
|  `KSTACK_SIZE`  | `8192`                | `include/kernel/task.h:56`  | 内核栈大小 (2 页)                      |
|  `MAX_ORDER`    | `9`                   | `include/kernel/buddy.h:25` | buddy 最大分配阶 (2^9 = 512 页 = 2 MB) |
|  `UART_BASE`    | `0x10000000`          | `include/drivers/uart.h:32` | UART MMIO 基地址                       |

### 1.2 虚拟-物理地址转换

内核通过直接线性映射（direct linear mapping）访问物理内存：每个物理地址 `pa` 对应虚拟地址 `KERNEL_VBASE + pa`。

```c
// include/asm/page.h
#define __pa(x) ((uintptr_t)(x) - KERNEL_VBASE)   // 虚拟 → 物理
#define __va(x) ((void *)((uintptr_t)(x) + KERNEL_VBASE))  // 物理 → 虚拟
```

这两个宏仅适用于通过 `KERNEL_VBASE` 直接映射的内核地址，不适用于 vmalloc 分配的地址或用户空间地址。

---

## 2. 物理内存布局

### 2.1 QEMU virt 机器物理地址空间

```
物理地址范围                        大小        描述
──────────────────────────────     ─────────   ──────────────────────────
0x00000000 .. 0x0FFFFFFF           1 GB        MMIO 设备区
  ├─ 0x00100000                                QEMU VIRT_TEST (关机/重启)
  ├─ 0x10000000                                UART (NS16550A)
  ├─ 0x0C000000                                SiFive PLIC (中断控制器)
  └─ 其他设备寄存器
0x80000000 .. 0x801FFFFF           ~2 MB       OpenSBI 固件 (M-mode)
0x80200000 .. _end                 可变        内核映像
  ├─ .text                                     内核代码
  ├─ .rodata                                   只读数据 + 嵌入的用户 ELF
  ├─ .data / .sdata                            已初始化数据
  └─ .bss                                      零初始化数据
       ├─ tmp_pgd (4096 B)                     启动临时页目录
       └─ boot_stack (4096 B)                  启动临时栈
_end .. page_table_mem_end()        可变        早期 bump 分配器分配的页表页
page_table_mem_end() .. mem_map_end 可变        struct page 数组 (mem_map[])
mem_map_end .. 0x90000000           可变        buddy 分配器管理的空闲物理页
0x90000000                          —           DRAM 结束 (DRAM_BASE + DRAM_SIZE)
```

### 2.2 内核映像内部布局

内核映像由链接脚本 `kernel.ld` 定义，链接地址为 `KERNEL_VBASE + BASE_ADDRESS = 0xFFFFFFC080200000`。

```
链接地址 (虚拟)                     段名         权限     内容
──────────────────────────────     ──────      ──────   ─────────────────────
0xFFFFFFC080200000                 .text        R|X      .text.entry 在前
                                                          (_start, __alltraps)
0xFFFFFFC08020xxxx                 .text        R|X      .text.* (其他内核代码)
0xFFFFFFC08020xxxx                 .rodata      R        .rodata.* (只读数据,
                                                          含嵌入的用户 ELF)
0xFFFFFFC08020xxxx                 .data        R|W      .data.*, .sdata.*
                                                          __global_pointer$ = . + 0x800
0xFFFFFFC08020xxxx                 .bss         R|W      __bss_start .. __bss_end
                                                          (.sbss, .bss, COMMON)
                                                          含 tmp_pgd[4096] 和 boot_stack[4096]
0xFFFFFFC08020xxxx                 _end         —        内核映像结束 (4KB 对齐)
```

链接脚本中 `AT(ADDR(.text) - KERNEL_VBASE)` 指定各段的物理加载地址（LMA），等于虚拟地址减去 `KERNEL_VBASE`。

### 2.3 用户程序链接布局

用户程序由 `user/user.ld` 链接，加载基址为 `0x10000`：

```
虚拟地址              段名        内容
─────────────        ──────     ──────────────────
0x00010000           .text       .text.entry 在前 (_start)
0x0001xxxx           .text       .text.* (用户代码)
0x0001xxxx           .rodata     .rodata.* (只读数据)
0x0001xxxx           .data       .data.*, .sdata.*
0x0001xxxx           .bss        __bss_start .. __bss_end
```

---

## 3. 内核虚拟内存映射

### 3.1 启动临时页表

启动阶段由 `arch/riscv/boot.S` 中的 `_start` 建立。此时 MMU 关闭，代码运行在物理地址空间。临时页表 `tmp_pgd` 是一个 512 项的 PGD，使用 1 GB 大页（mega page）映射：

```
PGD 索引    虚拟地址范围                      物理地址        权限         说明
─────────   ──────────────────────────────   ────────────   ──────────   ──────────────
PGD[0]      0x00000000 .. 0x3FFFFFFF         0x00000000     R+W+G       MMIO 设备空间
PGD[2]      0x80000000 .. 0xBFFFFFFF         0x80000000     R+W+X+G     DRAM 恒等映射
PGD[258]    0xFFFFFFC000000000+0x80000000..  0x80000000     R+W+X+G     DRAM 高地址映射
```

启动流程：
1. OpenSBI (M-mode) 跳转到 `_start`（物理地址 `0x80200000`）
2. 仅 hart 0 执行，其他 hart 进入 `wfi` 停泊
3. 设置 `sp = boot_stack_top`，清零 BSS 段
4. 构建 `tmp_pgd`，写入 `satp` CSR 开启 Sv39 MMU
5. 执行 `sfence.vma` 刷新 TLB
6. 通过 `jr` 跳转到 `kernel_main` 的高虚拟地址

### 3.2 正式内核页表

正式内核页表由 `kernel_pagetable_init()`（`arch/riscv/mm/page_table.c`）建立，使用 4 KB 细粒度页映射全部 256 MB DRAM：

```
PGD 索引    虚拟地址范围                                 物理地址       页大小    权限
─────────   ─────────────────────────────────────────   ────────────   ──────   ───────────
PGD[258]    0xFFFFFFC080000000 .. 0xFFFFFFC090000000     DRAM           4 KB    PTE_KERN_RWX
              └─ PMD → 128 个 PTE 页                                      (R+W+X+G+A+D)
PGD[2]      0x80000000 .. 0xBFFFFFFF                     DRAM           4 KB    PTE_KERN_RWX
              └─ 与 PGD[258] 共享同一 PMD 页
PGD[0]      0x00000000 .. 0x3FFFFFFF                     0x0            1 GB    PTE_KERN_RW
              └─ mega page                                                 (R+W+G+A+D)
```

**共享 PMD 设计**：PGD[2]（恒等映射）和 PGD[258]（高地址映射）指向同一个 PMD 页，因此两组映射使用完全相同的一组 PTE 页。这节省了 128 个 PTE 页（512 KB）的内存开销。

**切换策略**：新旧页表在切换时刻映射相同的虚拟地址范围，因此切换过程是无缝的——切换 `satp` 后指令流继续在相同的虚拟地址执行。

### 3.3 内核虚拟地址空间总览

```
虚拟地址                                          描述
──────────────────────────────────────────────    ──────────────────────────────
0x00000000 .. 0x3FFFFFFF                          MMIO 恒等映射 (1 GB mega page)
0x80000000 .. 0xBFFFFFFF                          DRAM 恒等映射 (启动过渡用)
0xFFFFFFC000000000 + 0x80000000                   DRAM 高地址直接映射起始
  0xFFFFFFC080200000                                内核 .text 入口
  0xFFFFFFC0802xxxxx                                .rodata, .data, .bss
  0xFFFFFFC0802xxxxx                                _end → 早期页表页
  0xFFFFFFC0802xxxxx                                mem_map[] 数组
  0xFFFFFFC0802xxxxx .. 0xFFFFFFC090000000          buddy 空闲物理页
0xFFFFFFC000000000 + 0x10000000                   UART MMIO (经 PGD[0] mega page)
```

---

## 4. Sv39 页表结构

### 4.1 地址分解

CuteOS 使用 RISC-V Sv39 分页模式，虚拟地址为 39 位（高 25 位为符号扩展），地址空间 512 GB。

```
虚拟地址格式：
[63:39] 符号扩展  |  [38:30] L2 索引 (PGD)  |  [29:21] L1 索引 (PMD)  |  [20:12] L0 索引 (PTE)  |  [11:0] 页内偏移

每级索引：9 位 (512 项)
页大小：4 KB (12 位偏移)
虚拟地址空间：2^39 = 512 GB
```

### 4.2 PTE 格式

```
PTE (64 位)：
[63:54] 保留  [53:10] PPN (44 位)  [9:8] RSW  [7] D  [6] A  [5] G  [4] U  [3] X  [2] W  [1] R  [0] V
```

### 4.3 权限标志位

定义于 `include/asm/pte.h`：

|  标志位 |  位置 |       说明         |
|---------|-------|--------------------|
| `PTE_V` | bit 0 | Valid，页表项有效  |
| `PTE_R` | bit 1 | Read，可读         |
| `PTE_W` | bit 2 | Write，可写        |
| `PTE_X` | bit 3 | Execute，可执行    |
| `PTE_U` | bit 4 | User，用户态可访问 |
| `PTE_G` | bit 5 | Global，全局映射   |
| `PTE_A` | bit 6 | Accessed，已访问   |
| `PTE_D` | bit 7 | Dirty，已写入      |

### 4.4 权限组合常量

```c
#define PTE_KERN_RW    (PTE_V | PTE_R | PTE_W | PTE_G | PTE_A | PTE_D)        // 内核数据
#define PTE_KERN_RWX   (PTE_V | PTE_R | PTE_W | PTE_X | PTE_G | PTE_A | PTE_D) // 内核代码
#define PTE_USER_RWX   (PTE_V | PTE_R | PTE_W | PTE_X | PTE_U | PTE_A | PTE_D) // 用户代码
#define PTE_USER_R     (PTE_V | PTE_R | PTE_U | PTE_A | PTE_D)                 // 用户只读
#define PTE_USER_RX    (PTE_V | PTE_R | PTE_X | PTE_U | PTE_A | PTE_D)         // 用户只读代码
#define PTE_USER_RW    (PTE_V | PTE_R | PTE_W | PTE_U | PTE_A | PTE_D)         // 用户数据/栈
```

### 4.5 PTE 操作宏

```c
#define PTE_PPN_SHIFT       10
#define PTE_PPN(pte)        ((pte) >> PTE_PPN_SHIFT)           // 提取 PPN
#define PTE_CREATE(pfn, flags) (((uint64_t)(pfn) << PTE_PPN_SHIFT) | (flags))  // 构造 PTE
#define PTE_TO_PA(pte)      ((uint64_t)PTE_PPN(pte) << PAGE_SHIFT)  // PTE → 物理地址
#define PA_TO_PTE(pa)       (PTE_CREATE(PHYS_PFN(pa), 0))      // 物理地址 → PPN 部分
```

### 4.6 页表遍历

`walk_page_table(pgd, va, alloc)` 实现三级遍历：

```
PGD 页 (4 KB, 512 项)
  │
  │ idx2 = (va >> 30) & 0x1FF
  ▼
PMD 页 (4 KB, 512 项)
  │
  │ idx1 = (va >> 21) & 0x1FF
  ▼
PTE 页 (4 KB, 512 项)
  │
  │ idx0 = (va >> 12) & 0x1FF
  ▼
叶子 PTE → PA_TO_PTE(pa) | perm
```

当 `alloc=true` 时，遍历过程中若中间级页表不存在则自动分配新页并安装。中间级页表项的标志为 `PTE_TABLE`（即 `PTE_V`，R=W=X=0），表示"指向下一级页表"而非叶子映射。

---

## 5. 物理内存管理

CuteOS 的物理内存管理分为四个层次，按初始化顺序逐层建立。

### 5.1 Layer 1：早期 Bump 分配器

**文件**：`arch/riscv/mm/page_table.c`

**用途**：在 buddy 分配器初始化之前，为内核正式页表分配页表页。

**机制**：
- 分配指针 `early_alloc_ptr` 从 `_end`（内核映像结束位置）开始，4 KB 对齐
- 每次分配一页，指针递增 `PAGE_SIZE`
- 分配的页清零后返回虚拟地址
- `page_table_mem_end()` 返回当前分配指针，供后续 `buddy_init()` 确定空闲内存起始位置

**生命周期**：仅在 `kernel_pagetable_init()` 期间使用。`buddy_init()` 完成后，页表分配器通过 `page_table_use_buddy()` 切换到 buddy 分配。

### 5.2 Layer 2：Buddy 分配器

**文件**：`mm/buddy.c`
**头文件**：`include/kernel/buddy.h`

**用途**：管理系统全部物理页帧的分配与释放，是所有上层分配器的基础。

**数据结构**：

```c
struct page {
    uint32_t flags;       // PG_RESERVED (保留) / PG_SLAB (SLAB 管理)
    uint32_t order;       // 在 buddy 中的分配阶数
    uint32_t refcount;    // 引用计数
    struct list_head lru; // 链表节点
};

struct free_area {
    struct list_head free_list;  // 空闲页块链表
    uint32_t nr_free;           // 空闲块数量
};

// 全局实例
struct page *mem_map;                    // 每个 PFN 对应一个 struct page
struct free_area free_area[MAX_ORDER + 1]; // 10 个阶 (0..9)
```

**初始化**（`buddy_init()`）：

```
物理内存布局（buddy_init 后）：

DRAM_BASE (0x80000000)
  │  内核映像 (.text, .rodata, .data, .bss)
  │  早期 bump 分配的页表页
  │
  ▼ page_table_mem_end()
  │  mem_map[0 .. total_pages-1]  (struct page 数组)
  │
  ▼ ALIGN_UP(mem_map_end, PAGE_SIZE)
  │  空闲物理页起始
  │  按最大对齐阶数分块挂入 free_area[]
  │
  ▼ 0x90000000 (DRAM_BASE + DRAM_SIZE)
```

- `total_pages = DRAM_SIZE / PAGE_SIZE = 65536` 页
- 所有页初始化为 `PG_RESERVED`
- `mem_map` 之前的页（内核映像、早期页表）保持 `PG_RESERVED`
- 空闲页从 `mem_map` 数组结束后开始，按最大对齐阶数分块加入对应 `free_area[order]`

**分配**（`get_free_page(order)`）：
- 从 `free_area[order]` 取一个空闲块
- 若该阶为空，从更高阶拆分（split down），直到目标阶数
- 返回首页的内核虚拟地址（通过 `__va` 转换），失败返回 NULL

**释放**（`free_page(addr, order)`）：
- 将页块标记为空闲
- 计算伙伴地址：`buddy_pfn = pfn ^ (1 << order)`
- 若伙伴空闲且同阶，则合并并升阶，递归直到无法合并或达到 `MAX_ORDER`

### 5.3 Layer 3：SLAB 分配器

**文件**：`mm/slab.c`
**头文件**：`include/kernel/slab.h`

**用途**：在 buddy 之上提供小对象的快速分配（kmalloc/kfree）。

**大小级别**：8 个固定缓存，对应对象大小 16 / 32 / 64 / 128 / 256 / 512 / 1024 / 2048 字节。

**Slot 布局**：

```
+0    uint32_t cache_idx   ← 所属缓存索引，kfree 时定位缓存
+4    uint32_t _pad        ← 对齐填充
+8    用户数据 / list_head  ← 空闲时前 16 字节作为链表节点
+8+N  ...
```

**分配流程**（`kmalloc(size)`）：
1. 遍历 8 个缓存，找到第一个 `obj_size >= size` 的缓存
2. 从 `free_list` 取出空闲对象返回
3. 若 `free_list` 为空，向 buddy 请求一页，按 slot 大小切割为多个对象挂入 `free_list`

**释放流程**（`kfree(ptr)`）：
1. 读取 `ptr - 8` 处的 `cache_idx`，确定所属缓存
2. 将对象归还到对应缓存的 `free_list`

**限制**：释放的对象不归还物理页给 buddy 分配器（简化实现）。

### 5.4 Layer 4：vmalloc 分配器

**文件**：`mm/vmalloc.c`

**用途**：管理 128 MB 内核虚拟地址区域，用于分配虚拟连续但物理不连续的大块内存。

**当前状态**：仅有接口声明和文档注释，实现为 stub。

---

## 6. 用户地址空间管理

### 6.1 用户地址空间结构

每个用户进程拥有一个 `struct mm_struct`（定义于 `include/kernel/mm.h`）描述其地址空间：

```c
struct mm_struct {
    pte_t *pgd;                          // 用户页表根
    uintptr_t brk;                       // 当前堆顶
    uintptr_t code_start;                // 代码段起始
    uintptr_t code_end;                  // 代码段结束（页对齐）
    struct vm_area_struct vma[NR_VMA];   // VMA 固定数组（16 项）
};
```

VMA（虚拟内存区域）描述一段连续的虚拟地址：

```c
struct vm_area_struct {
    uintptr_t vm_start;  // 区域起始（含）
    uintptr_t vm_end;    // 区域结束（不含）
    uint32_t vm_flags;   // VM_READ | VM_WRITE | VM_EXEC
    bool used;           // 槽位是否在用
};
```

### 6.2 用户虚拟地址空间布局

```
用户虚拟地址                  描述                          权限
─────────────────────        ──────────────────────────    ───────────────────
0x00010000                   ELF 加载基地址 (user.ld)
  ├─ .text (.text.entry)     用户代码 (_start)              PTE_USER_RWX / PTE_USER_RX
  ├─ .rodata                 只读数据                       PTE_USER_R
  ├─ .data / .sdata          已初始化数据                   PTE_USER_RW
  └─ .bss                    BSS 段                         PTE_USER_RW

code_end (页对齐)             初始 brk 位置
  │
  │ [堆 — 通过 brk 系统调用向上增长]    PTE_USER_RW (惰性分配)
  │
  │ ... 空闲区间 ...
  │
  │ [匿名 mmap — 默认从高地址向下分配]      PTE_USER_* (惰性分配)
  │
0x7FFFE000 .. 0x7FFFEFFF     signal trampoline (1 页)       PTE_USER_RX
  │
0x7FFFF000 .. 0x7FFFFFFF     用户栈 (1 页, 4096 字节)       PTE_USER_RW
0x80000000                   USER_STACK_TOP (初始 SP 值)     — (TASK_SIZE 上限)
```

- 用户 ELF 从 `0x10000` 加载，各 PT_LOAD 段按 ELF `p_vaddr` 映射
- 栈固定 1 页（`0x7FFFF000 .. 0x80000000`），初始 SP 设为 `0x80000000`
- 堆从 `code_end`（最后一个 PT_LOAD 段结束的页对齐地址）开始，通过 `brk` 系统调用扩展
- `SIGNAL_TRAMPOLINE_ADDR` 固定为 `0x7FFFE000`，映射 1 页只读可执行用户页。信号处理函数返回时通过该页执行 `sigreturn` 系统调用
- 匿名 `mmap` 自动选址必须避开 signal trampoline 页。当前从 `SIGNAL_TRAMPOLINE_ADDR` 下方开始向低地址搜索，例如 `mmap(0, 8192, ...)` 会返回 `0x7FFFC000 .. 0x7FFFE000`，不会覆盖 `0x7FFFE000 .. 0x7FFFF000`

### 6.3 用户页表结构

用户页表由 `mm_create_user_pgd()`（`mm/mmap.c`）创建：

**PGD 布局**：
```
PGD[0 .. 255]    用户空间部分 — 代码、数据、堆、mmap、trampoline、栈
PGD[256 .. 511]  内核映射 — 从内核 PGD 原样复制（共享内核高地址映射）
```

**设计要点**：
- 每个用户进程的 `PGD[256..511]` 直接复制内核 PGD 的对应条目，确保 trap 进入内核后无需切换页表即可访问内核代码和数据
- UART MMIO（`0x10000000`）也映射到用户页表中，因为 trap 处理中的 `printk` 使用 UART MMIO
- signal trampoline 页由 `mm_create_user_pgd()` 固定映射到 `SIGNAL_TRAMPOLINE_ADDR`，所有用户地址空间共享同一个只读可执行物理页。销毁用户页表时跳过该共享页，不归还给 buddy
- 用户地址空间上限为 `TASK_SIZE`（`0x80000000`），即低 2 GB

### 6.4 ELF 加载流程

`exec_user_elf()`（`kernel/exec.c`）完成从内核线程到用户进程的转换：

1. **ELF 校验**：检查 magic、class (ELF64)、endianness (LSB)、type (ET_EXEC)、machine (EM_RISCV)
2. **创建地址空间**：`mm_alloc()` + `mm_create_user_pgd()`
3. **映射 PT_LOAD 段**：逐页分配物理页，复制 `filesz` 字节数据，`memsz - filesz` 部分清零，为每段创建 VMA
4. **设置代码段范围**：`code_start` = 第一个段起始，`code_end` = 最后段结束的页对齐值，`brk` = `code_end`
5. **分配用户栈**：1 页，映射到 `USER_STACK_BASE`（`0x7FFFF000`），创建栈 VMA
6. **构造 trap_frame**：`sepc` = ELF 入口，`sp` = `USER_STACK_TOP`，`sstatus.SPP` = 0（返回 U-mode）
7. **切换到用户态**：写入 `satp` → `sfence.vma` → 设置 `sscratch` → 切换 `sp` → 跳转 `__trapret` → `sret` 进入用户态

此函数不返回（标记 `unreachable()`）。

### 6.5 堆管理（brk）

`mm_brk()`（`mm/mmap.c`）实现 `brk` 系统调用的内核侧逻辑：

- **查询**：传入 `addr=0` 返回当前 `brk` 值
- **扩展**：传入新地址，不允许缩小，不允许超过 `TASK_SIZE`
- **惰性分配**：仅更新 VMA 边界和 `brk` 指针，不分配物理页。实际的物理页在缺页异常时由 `do_page_fault()` 按需分配
- **首次扩展**：若当前没有覆盖 `brk` 的 VMA，创建新的堆 VMA（`VM_READ | VM_WRITE`）
- **后续扩展**：找到覆盖旧 `brk` 的 VMA，扩展其 `vm_end`

### 6.5.1 匿名 mmap 自动选址

`mm_mmap()` 当前只支持匿名私有映射。`addr == 0` 时，内核从高地址向低地址搜索空闲区间，但最高搜索边界不是 `USER_STACK_BASE`，而是 `SIGNAL_TRAMPOLINE_ADDR`。

```
0x80000000  USER_STACK_TOP / TASK_SIZE
0x7FFFF000  USER_STACK_BASE            [用户栈, RW, 1 页]
0x7FFFE000  SIGNAL_TRAMPOLINE_ADDR     [signal trampoline, RX, 1 页]
0x7FFFD000                             mmap 自动选址的高端边界以下
0x7FFFC000                             mmap(0, 8192, ...) 示例起点
```

设计约束：

- 普通 VMA 不得覆盖 signal trampoline 页
- `MAP_FIXED` 或带 hint 的 `mmap` 若范围与 trampoline 页重叠，返回 `-EINVAL`
- 自动选址从 `SIGNAL_TRAMPOLINE_ADDR - length` 开始向低地址寻找空洞，避免把普通匿名页映射到 trampoline 固定地址

### 6.6 缺页处理

`do_page_fault()`（`mm/page_fault.c`）是缺页异常的处理入口。

**处理流程**：

```
缺页异常 (scause = 12/13/15)
  │
  ▼
读取 fault_addr = tf->stval, scause = tf->scause
  │
  ▼
find_vma(mm, fault_addr)
  │
  ├─ 未找到 VMA → do_exit(1) (非法访问)
  │
  ▼
check_vma_permission(scause, vma)
  │
  ├─ 权限不匹配 → do_exit(1) (权限冲突)
  │
  ▼
检查是否已映射 (walk_page_table, alloc=false)
  │
  ├─ 已映射且 PTE 权限匹配 → sfence.vma 刷新 TLB 后返回
  │                         (可能 TLB 一致性问题)
  │
  ├─ 已映射但 PTE 权限不匹配 → do_exit(1) (保护错误)
  │
  ▼
分配物理页 (get_free_page(0))
  │  清零 (memset, 防止内核数据泄漏)
  ▼
建立映射 (map_page)
  │  权限由 VMA flags 转换: vma_flags_to_pte()
  ▼
刷新 TLB (sfence_vma_addr)
  │
  ▼
返回 → 缺页指令重新执行
```

**缺页类型与权限检查**：

| scause |              异常类型             | 检查的 VMA 权限 |
|--------|-----------------------------------|-----------------|
| 12     | 指令缺页 (Instruction page fault) | `VM_EXEC`       |
| 13     | 加载缺页 (Load page fault)        | `VM_READ`       |
| 15     | 存储缺页 (Store/AMO page fault)   | `VM_WRITE`      |

**内核态缺页**：支持内核态下由 `copy_to_user` / `copy_from_user` 触发的缺页，此时同样为对应用户页分配物理页。若内核态访问非法地址则 `do_exit`。

**VMA 与 PTE 权限同时检查**：VMA 表示该虚拟范围的期望权限；若页表中已经存在 PTE，则还必须检查 PTE 是否允许当前 fault 类型。比如 signal trampoline 页是 `PTE_USER_RX`，即使错误的 VMA 覆盖到该地址，用户写入也不能靠反复刷新 TLB 重试，否则会形成无限 page fault。

### 6.7 进程地址空间复制（fork）

`sys_fork()` / `do_fork()`（`kernel/fork.c`）通过完整物理复制创建子进程。

**复制内容**：

| 组件 | 复制策略 |
|---|---|
| mm_struct | `kmalloc` 分配新的，深拷贝所有字段 |
| 用户页表 | 分配新 PGD，复制内核映射 (`PGD[256..511]`) |
| 物理页 | 遍历每个 VMA，为每页分配新物理页并复制内容 |
| VMA 数组 | 深拷贝 16 项 `vma[]` 数组 |
| trap_frame | 完整复制，但子进程 `a0 = 0`（fork 在子进程返回 0） |
| 文件描述符 | 复制 `fd_array[32]`，每个已打开文件 `refcount++` |
| 信号处理器 | 复制 `sigactions[NSIG]` sigaction 数组 |
| 待处理信号 | 不复制，子进程 `pending` 清零 |

子进程创建后加入就绪队列尾部，父进程先运行。

---

## 7. 物理页描述符

每个物理页帧对应一个 `struct page` 实例，存储在全局数组 `mem_map[]` 中。

**定义**（`include/kernel/page.h`）：

```c
#define PG_RESERVED  0   // 保留给内核（不可分配）
#define PG_SLAB      1   // 由 SLAB 分配器管理

struct page {
    uint32_t flags;       // 页状态标志位
    uint32_t order;       // buddy 中的分配阶数
    uint32_t refcount;    // 引用计数
    struct list_head lru; // buddy 空闲链表节点
};
```

**PFN 转换**（`mm/buddy.c`）：

```c
pfn = page - mem_map;              // struct page → PFN
page = &mem_map[pfn];              // PFN → struct page
va = __va(DRAM_BASE + pfn * PAGE_SIZE);  // PFN → 内核虚拟地址
pfn = (__pa(va) - DRAM_BASE) / PAGE_SIZE; // 内核虚拟地址 → PFN
```

---

## 8. 内核栈

每个任务（内核线程或用户进程）拥有独立的内核栈。

```c
#define KSTACK_ORDER 1            // 2^1 = 2 页
#define KSTACK_SIZE  (PAGE_SIZE << KSTACK_ORDER)  // 8192 字节
```

- 内核栈由 buddy 分配器分配（`get_free_page(KSTACK_ORDER)`），大小 8 KB（2 页）
- 栈底写入 `CANARY_MAGIC`（`0xDEADBEEFDEADBEEF`），用于检测栈溢出
- `check_canary()` 在调度等关键路径检查 canary 是否被破坏
- trap_frame 构造在内核栈顶部（高地址向下），`__trapret` 从 trap_frame 恢复上下文

---

## 9. 启动初始化流程

`kernel_main()`（`init/main.c`）按严格的依赖顺序初始化各子系统：

```
OpenSBI (M-mode)
  │ 跳转到 _start @ PA 0x80200000
  ▼
boot.S (_start)
  │ 1. 仅 hart 0 继续，其余 wfi 停泊
  │ 2. sp = boot_stack_top, 清零 BSS
  │ 3. 构建 tmp_pgd (3 个 1GB mega page)
  │ 4. 写 satp, sfence.vma, 开启 Sv39 MMU
  │ 5. jr kernel_main (高虚拟地址)
  ▼
kernel_main()
  │ console_init_sbi()           printk via SBI ecall
  │ kernel_pagetable_init()      正式 4KB 内核页表，切换 satp
  │ console_init_mmio()          printk 切换到 UART MMIO
  │ buddy_init()                 buddy 物理页分配器
  │ page_table_use_buddy()       页表分配器切换到 buddy
  │ slab_init()                  kmalloc/kfree 可用
  │ trap_init()                  stvec = __alltraps, 中断使能
  │ task_init()                  idle 任务 (PID 0, BSS 静态分配)
  │ timer_init()                 Sstc stimecmp 首次时钟中断
  │ sched_init()                 全局就绪队列
  │ syscall_init()               系统调用入口
  │ kernel_thread(init_process)  PID 1 init 线程
  │ while(1) { wfi(); schedule(); }  idle 循环
  ▼
init_process()
  │ exec_user_elf()  加载嵌入的用户 ELF
  │ 创建用户地址空间，sret 进入用户态
  ▼
User Mode
  │ _start (user/start.S)
  │ 清零 fp, 调用 main(0, NULL)
  │ 退出时 ecall SYS_exit
```

**子系统依赖关系**：

```
console → pagetable → buddy → slab → trap → task → timer → sched → syscall → init
```

每个子系统的初始化严格在其依赖项之后。不解析 DTB，所有硬件参数（`DRAM_BASE`、`DRAM_SIZE`、设备地址）在编译时硬编码。

---

## 10. 完整内存布局图

### 10.1 内核虚拟地址空间

```
0xFFFFFFC000000000 ┌─────────────────────────────────────────────┐
                   │                                             │
                   │  内核直接映射区 (256 MB DRAM)               │
                   │  虚拟地址 = KERNEL_VBASE + 物理地址         │
                   │  4 KB 页, PTE_KERN_RWX                      │
                   │                                             │
0xFFFFFFC080000000 │  DRAM 物理起始处                            │
                   │    OpenSBI 固件区域 (未使用但已映射)        │
0xFFFFFFC080200000 │    内核映像起始                             │
                   │    ├─ .text (代码)                          │
                   │    ├─ .rodata (只读数据 + 嵌入用户 ELF)     │
                   │    ├─ .data / .sdata (已初始化数据)         │
                   │    └─ .bss (tmp_pgd + boot_stack + 全局变量)│
0xFFFFFFC08020xxxx │  _end (内核映像结束)                        │
                   │    早期 bump 分配的页表页                   │
                   │    mem_map[] 数组                           │
                   │    buddy 空闲物理页 ...                     │
0xFFFFFFC090000000 ├─────────────────────────────────────────────┤  DRAM 映射结束
                   │  (未映射区域)                               │
                   │                                             │
0x00000000xxxxxxxx │  MMIO 恒等映射 (1 GB mega page)             │  PGD[0]
                   │  UART @ 0x10000000                          │
                   └─────────────────────────────────────────────┘
```

### 10.2 用户虚拟地址空间

```
0x00000000    ┌─────────────────────────────────────────────────┐
              │                                                 │
              │  (0x0 ~ 0x0FFFF 为空，未映射)                   │
              │                                                 │
0x00010000    │  ELF 加载基地址                                 │
              │    ├─ .text (.text.entry: _start)               │
              │    ├─ .rodata                                   │
              │    ├─ .data / .sdata                            │
              │    └─ .bss                                      │
              │                                                 │
code_end      │  初始 brk = code_end (页对齐)                   │
              │                                                 │
              │    [堆 —— brk 向上增长]                         │
              │    物理页通过缺页按需分配                       │
              │                                                 │
              │    ... 空闲区间 ...                             │
              │                                                 │
0x7FFFC000    │    示例: mmap(0, 8192) 自动选址结果             │
              │    [匿名 mmap — 2 页, RW, 惰性分配]             │
              │                                                 │
0x7FFFE000    │    [signal trampoline — 1 页, RX]               │
              │    普通 mmap 不得覆盖该页                       │
              │                                                 │
0x7FFFF000    │    [用户栈 — 1 页, 4096 字节]                   │
0x80000000    │    USER_STACK_TOP (初始 SP 值)                  │
              │    = TASK_SIZE (用户地址空间上限)               │
              └─────────────────────────────────────────────────┘

              用户 PGD[256..511] = 内核 PGD[256..511] (共享内核映射)
              用户 PGD[0..255]   = 用户代码/数据/堆/mmap/trampoline/栈
```

### 10.3 物理内存布局

```
物理地址                      描述
───────────────────────      ──────────────────────────────────────
0x00000000 .. 0x0FFFFFFF     MMIO 设备区 (1 GB)
0x10000000                   UART (NS16550A)
0x80000000 .. 0x801FFFFF     OpenSBI 固件 (M-mode)
0x80200000                   内核映像加载地址
  .text, .rodata, .data, .bss
  tmp_pgd[4096], boot_stack[4096]
_end                         内核映像结束
  [早期 bump 分配的页表页]
page_table_mem_end()
  mem_map[0 .. 65535]        struct page 数组 (65536 项)
mem_map_end (页对齐)
  [buddy 空闲页 ...]
0x90000000                   DRAM 结束
```

---

## 11. 关键源文件索引

|            文件路径          |                          功能                                 |
|------------------------------|---------------------------------------------------------------|
| `kernel.ld`                  | 内核链接脚本 — 段布局、基址、导出符号                         |
| `user/user.ld`               | 用户程序链接脚本 — 基址 `0x10000`                             |
| `include/asm/page.h`         | `PAGE_SIZE`、`DRAM_BASE`、`KERNEL_VBASE`、`__pa`/`__va`       |
| `include/asm/pte.h`          | Sv39 PTE 位定义、权限常量、`walk_page_table`/`map_page` 声明  |
| `include/kernel/mm.h`        | `mm_struct`、`vm_area_struct`、VMA/brk/uaccess/pfault 接口    |
| `include/kernel/page.h`      | `struct page` — 物理页帧描述符                                |
| `include/kernel/buddy.h`     | `MAX_ORDER`、`free_area`、buddy 接口                          |
| `include/kernel/task.h`      | `task_struct`、`KSTACK_SIZE`、任务管理接口                    |
| `include/asm/trap.h`         | `trap_frame`、`context`、异常码                               |
| `include/asm/csr.h`          | `SATP_MODE_SV39`、CSR 读写宏                                  |
| `arch/riscv/boot.S`          | 启动：临时页表、MMU 开启、跳转 `kernel_main`                  |
| `arch/riscv/entry.S`         | Trap 入口/出口、上下文切换                                    |
| `arch/riscv/mm/page_table.c` | 正式内核页表初始化、页表遍历、映射操作                        |
| `arch/riscv/mm/tlb.c`        | `sfence.vma` TLB 刷新操作                                     |
| `mm/buddy.c`                 | Buddy 物理页分配器                                            |
| `mm/slab.c`                  | SLAB `kmalloc`/`kfree`                                        |
| `mm/mmap.c`                  | 用户地址空间管理：mm_alloc/destroy、VMA、brk                  |
| `mm/page_fault.c`            | 缺页异常处理（惰性分配）                                      |
| `mm/uaccess.c`               | 用户空间安全访问：`access_ok`、`copy_to/from_user`            |
| `mm/vmalloc.c`               | vmalloc 区域管理（当前为 stub）                               |
| `kernel/exec.c`              | ELF 加载、用户栈设置、切换到用户态                            |
| `kernel/fork.c`              | 进程创建（完整物理复制）                                      |
| `kernel/init_process.c`      | PID 1 init 线程                                               |
| `init/main.c`                | `kernel_main` — 内核初始化入口                                |
| `user/start.S`               | 用户态 `_start` 入口                                          |
| `user/include/user.h`        | 用户态系统调用封装                                            |
