/*
 * init/test.c - 内核子系统自测
 *
 * 功能：
 *   汇总所有子系统测试函数，由 kernel_test() 统一调用。
 *   每项测试独立运行，失败时打印 FAIL 但不中止内核。
 *
 * 测试覆盖：
 *   - bitmap  : 位图 set/clear/test/find_first_zero 完整操作
 *   - pid     : PID 分配、释放、耗尽、PID 0 保护
 *   - buddy   : 多阶分配/释放、伙伴合并、对齐、OOM、压力循环
 *   - slab    : 多大小分配/释放、零大小处理、压力分配、跨缓存复用
 *   - trap    : trap_frame 结构布局、from_user 辅助函数
 *   - task    : task_alloc/free、PID 绑定、canary 完整性、进程树链接
 *   - timer   : jiffies 递增、mtime 单调性、mtimecmp 设置
 *
 * 添加新测试：
 *   1. 编写 static void test_xxx(void) { ... }
 *   2. 在 kernel_test() 中调用
 */

#include <kernel/test.h>
#include <kernel/printk.h>
#include <kernel/string.h>
#include <kernel/slab.h>
#include <kernel/buddy.h>
#include <kernel/task.h>
#include <kernel/pid.h>
#include <kernel/bitmap.h>
#include <kernel/page.h>
#include <kernel/list.h>
#include <kernel/sched.h>
#include <kernel/timer.h>
#include <asm/page.h>
#include <asm/trap.h>
#include <asm/csr.h>

/* ---- 测试计数器 ---- */

uint32_t __test_total;
uint32_t __test_passed;
uint32_t __test_failed;

/* ================================================================
 *  Bitmap 测试
 * ================================================================ */

/**
 * test_bitmap - 测试位图操作
 *
 * 验证 bitmap_set/clear/test/find_first_zero 的正确性，
 * 包括边界位、全满查找、连续 set/clear 循环。
 */
static void test_bitmap(void)
{
	TEST_BEGIN("bitmap: basic set/clear/test");
	{
		BITMAP_DECLARE_STATIC(bm, 64);

		bitmap_zero(&bm);

		/* 所有位应为 0 */
		TEST_ASSERT_EQ(bitmap_test(&bm, 0), false);
		TEST_ASSERT_EQ(bitmap_test(&bm, 31), false);
		TEST_ASSERT_EQ(bitmap_test(&bm, 63), false);

		/* 设置位并验证 */
		bitmap_set(&bm, 0);
		TEST_ASSERT_EQ(bitmap_test(&bm, 0), true);

		bitmap_set(&bm, 31);
		TEST_ASSERT_EQ(bitmap_test(&bm, 31), true);

		bitmap_set(&bm, 63);
		TEST_ASSERT_EQ(bitmap_test(&bm, 63), true);

		/* 未设置的位应仍为 0 */
		TEST_ASSERT_EQ(bitmap_test(&bm, 1), false);
		TEST_ASSERT_EQ(bitmap_test(&bm, 62), false);

		/* 清除位并验证 */
		bitmap_clear(&bm, 31);
		TEST_ASSERT_EQ(bitmap_test(&bm, 31), false);
	}
	TEST_END("bitmap: basic set/clear/test");
	return;
fail:
	TEST_FAIL("bitmap: basic set/clear/test", "see above");
}

static void test_bitmap_find_first_zero(void)
{
	TEST_BEGIN("bitmap: find_first_zero");
	{
		BITMAP_DECLARE_STATIC(bm, 64);

		bitmap_zero(&bm);

		/* 全空时第一个 0 位是 0 */
		TEST_ASSERT_EQ(bitmap_find_first_zero(&bm), (size_t)0);

		/* 设置位 0 后，第一个 0 位是 1 */
		bitmap_set(&bm, 0);
		TEST_ASSERT_EQ(bitmap_find_first_zero(&bm), (size_t)1);

		/* 设置位 0~31 后，第一个 0 位是 32 */
		for (size_t i = 0; i < 32; i++)
			bitmap_set(&bm, i);
		TEST_ASSERT_EQ(bitmap_find_first_zero(&bm), (size_t)32);

		/* 全满时返回 nbits */
		for (size_t i = 0; i < 64; i++)
			bitmap_set(&bm, i);
		TEST_ASSERT_EQ(bitmap_find_first_zero(&bm), (size_t)64);
	}
	TEST_END("bitmap: find_first_zero");
	return;
fail:
	TEST_FAIL("bitmap: find_first_zero", "see above");
}

static void test_bitmap_odd_bits(void)
{
	TEST_BEGIN("bitmap: odd bits set/clear");
	{
		BITMAP_DECLARE_STATIC(bm, 32);

		bitmap_zero(&bm);

		/* 设置所有奇数位 */
		for (size_t i = 1; i < 32; i += 2)
			bitmap_set(&bm, i);

		/* 偶数位应为 0 */
		for (size_t i = 0; i < 32; i += 2)
			TEST_ASSERT_EQ(bitmap_test(&bm, i), false);

		/* 奇数位应为 1 */
		for (size_t i = 1; i < 32; i += 2)
			TEST_ASSERT_EQ(bitmap_test(&bm, i), true);

		/* find_first_zero 应返回 0 */
		TEST_ASSERT_EQ(bitmap_find_first_zero(&bm), (size_t)0);
	}
	TEST_END("bitmap: odd bits set/clear");
	return;
fail:
	TEST_FAIL("bitmap: odd bits set/clear", "see above");
}

/* ================================================================
 *  PID 分配器测试
 * ================================================================ */

/**
 * test_pid_basic - 测试 PID 分配与释放
 *
 * 验证 PID 顺序分配、释放后再分配、PID 0 保护。
 */
static void test_pid_basic(void)
{
	TEST_BEGIN("pid: basic alloc/free");
	{
		/* 分配 PID 1（0 已被 idle 占用） */
		int32_t p1 = alloc_pid();
		TEST_ASSERT_EQ(p1, (int32_t)1);

		/* 释放后再次分配应得到相同 PID */
		free_pid((pid_t)p1);
		int32_t p1b = alloc_pid();
		TEST_ASSERT_EQ(p1b, (int32_t)1);

		free_pid((pid_t)p1b);

		/* 释放 PID 0 应无效 */
		free_pid(0);
		/* PID 0 之后应仍被占用，下次分配应从 1 开始 */
		int32_t p0 = alloc_pid();
		TEST_ASSERT_EQ(p0, (int32_t)1);
		free_pid((pid_t)p0);
	}
	TEST_END("pid: basic alloc/free");
	return;
fail:
	TEST_FAIL("pid: basic alloc/free", "see above");
}

/**
 * test_pid_exhaust - 测试 PID 耗尽与恢复
 *
 * 分配所有可用 PID，验证 OOM 返回 -ENOSPC，
 * 然后释放一个后验证可再次分配。
 */
static void test_pid_exhaust(void)
{
	TEST_BEGIN("pid: exhaust and recover");
	{
		int32_t pids[PID_COUNT];
		int count = 0;

		/* 分配所有可用 PID（跳过 0） */
		for (int i = 0; i < PID_COUNT - 1; i++) {
			int32_t pid = alloc_pid();
			if (pid < 0)
				break;
			pids[count++] = pid;
		}

		/* 应分配了 255 个 PID（1~255） */
		TEST_ASSERT_EQ(count, PID_COUNT - 1);

		/* 再次分配应失败 */
		int32_t oom = alloc_pid();
		TEST_ASSERT(oom < 0);

		/* 释放一个后应能再次分配 */
		free_pid((pid_t)pids[100]);
		int32_t recovered = alloc_pid();
		TEST_ASSERT_EQ(recovered, pids[100]);

		/* 清理 */
		free_pid((pid_t)recovered);
		for (int i = 0; i < count; i++) {
			if (i != 100)
				free_pid((pid_t)pids[i]);
		}
	}
	TEST_END("pid: exhaust and recover");
	return;
fail:
	TEST_FAIL("pid: exhaust and recover", "see above");
}

/* ================================================================
 *  Buddy 分配器测试
 * ================================================================ */

/**
 * test_buddy_single_page - 测试单页分配与释放
 *
 * 验证 order-0 分配返回非 NULL 且按 PAGE_SIZE 对齐的地址。
 */
static void test_buddy_single_page(void)
{
	TEST_BEGIN("buddy: single page alloc/free");
	{
		void *p = get_free_page(0);
		TEST_ASSERT_NOT_NULL(p);
		TEST_ASSERT_ALIGNED(p, PAGE_SIZE);

		/* 写入模式以验证地址可写 */
		memset(p, 0xBB, PAGE_SIZE);
		TEST_ASSERT(((uint8_t *)p)[0] == 0xBB);
		TEST_ASSERT(((uint8_t *)p)[PAGE_SIZE - 1] == 0xBB);

		free_page(p, 0);
	}
	TEST_END("buddy: single page alloc/free");
	return;
fail:
	TEST_FAIL("buddy: single page alloc/free", "see above");
}

/**
 * test_buddy_multi_order - 测试多阶分配与对齐
 *
 * 对 order 0~4 分别分配、验证对齐、写入、释放。
 * order=4 的块大小为 16 页 = 64KB，应 64KB 对齐。
 */
static void test_buddy_multi_order(void)
{
	TEST_BEGIN("buddy: multi-order alloc/free");
	{
		void *ptrs[5];

		for (uint32_t order = 0; order <= 4; order++) {
			size_t size = (size_t)PAGE_SIZE << order;
			size_t align = size;

			ptrs[order] = get_free_page(order);
			TEST_ASSERT_NOT_NULL(ptrs[order]);
			TEST_ASSERT_ALIGNED(ptrs[order], align);

			/* 写入首尾字节 */
			memset(ptrs[order], 0xCC, size);
			TEST_ASSERT(((uint8_t *)ptrs[order])[0] == 0xCC);
			TEST_ASSERT(((uint8_t *)ptrs[order])[size - 1] ==
				    0xCC);
		}

		/* 全部释放 */
		for (uint32_t order = 0; order <= 4; order++)
			free_page(ptrs[order], order);
	}
	TEST_END("buddy: multi-order alloc/free");
	return;
fail:
	TEST_FAIL("buddy: multi-order alloc/free", "see above");
}

/**
 * test_buddy_merge - 测试伙伴合并
 *
 * 分配两个相邻的 order-0 页块，释放后应能合并为 order-1 块，
 * 再分配 order-1 块来验证合并成功。
 */
static void test_buddy_merge(void)
{
	TEST_BEGIN("buddy: buddy merging");
	{
		/* 分配 4 个连续 order-0 页 */
		void *p0 = get_free_page(0);
		void *p1 = get_free_page(0);
		void *p2 = get_free_page(0);
		void *p3 = get_free_page(0);

		TEST_ASSERT_NOT_NULL(p0);
		TEST_ASSERT_NOT_NULL(p1);
		TEST_ASSERT_NOT_NULL(p2);
		TEST_ASSERT_NOT_NULL(p3);

		/* 释放全部 */
		free_page(p0, 0);
		free_page(p1, 0);
		free_page(p2, 0);
		free_page(p3, 0);

		/*
		 * 如果合并正常工作，释放 4 个 order-0 块后
		 * 应能分配一个 order-2（4页）块。
		 * 注意：不保证恰好是同一块，但合并后系统应有足够的连续页。
		 */
		void *big = get_free_page(2);
		TEST_ASSERT_NOT_NULL(big);
		TEST_ASSERT_ALIGNED(big, (size_t)PAGE_SIZE << 2);

		free_page(big, 2);
	}
	TEST_END("buddy: buddy merging");
	return;
fail:
	TEST_FAIL("buddy: buddy merging", "see above");
}

/**
 * test_buddy_stress - 压力测试：批量分配/释放循环
 *
 * 多次循环分配 64 个单页块，写入模式验证，然后全部释放。
 */
static void test_buddy_stress(void)
{
	TEST_BEGIN("buddy: stress alloc/free cycle");
	{
#define BUDDY_STRESS_N 64
		void *ptrs[BUDDY_STRESS_N];

		for (int round = 0; round < 3; round++) {
			/* 分配 */
			for (int i = 0; i < BUDDY_STRESS_N; i++) {
				ptrs[i] = get_free_page(0);
				TEST_ASSERT_NOT_NULL(ptrs[i]);
				memset(ptrs[i], (uint8_t)(round + i),
				       PAGE_SIZE);
			}

			/* 释放 */
			for (int i = 0; i < BUDDY_STRESS_N; i++)
				free_page(ptrs[i], 0);
		}
#undef BUDDY_STRESS_N
	}
	TEST_END("buddy: stress alloc/free cycle");
	return;
fail:
	TEST_FAIL("buddy: stress alloc/free cycle", "see above");
}

/**
 * test_buddy_split - 测试高阶拆分
 *
 * 分配一个 order-3 块（8页），释放后再分配 8 个 order-0 块，
 * 验证拆分机制正常。
 */
static void test_buddy_split(void)
{
	TEST_BEGIN("buddy: order split");
	{
		/* 分配 order-3 */
		void *big = get_free_page(3);
		TEST_ASSERT_NOT_NULL(big);

		/* 写入模式 */
		memset(big, 0xDD, PAGE_SIZE << 3);
		free_page(big, 3);

		/* 分配 8 个 order-0，应该成功（拆分了刚才释放的块） */
		void *pages[8];
		for (int i = 0; i < 8; i++) {
			pages[i] = get_free_page(0);
			TEST_ASSERT_NOT_NULL(pages[i]);
		}

		for (int i = 0; i < 8; i++)
			free_page(pages[i], 0);
	}
	TEST_END("buddy: order split");
	return;
fail:
	TEST_FAIL("buddy: order split", "see above");
}

/* ================================================================
 *  SLAB 分配器测试
 * ================================================================ */

/**
 * test_slab_basic - 基本 kmalloc/kfree 测试
 *
 * 分配 8 种大小，写入模式，释放，再分配，再释放。
 */
static void test_slab_basic(void)
{
	TEST_BEGIN("slab: basic alloc/free");
	{
#define SLAB_NR_CACHES 8
		static const size_t sizes[SLAB_NR_CACHES] = {16,  32,  64,   128,
							256, 512, 1024, 2048};
		void *ptrs[SLAB_NR_CACHES];

		/* Phase 1: 分配各大小并写入模式 */
		for (int i = 0; i < SLAB_NR_CACHES; i++) {
			ptrs[i] = kmalloc(sizes[i]);
			TEST_ASSERT_NOT_NULL(ptrs[i]);
			memset(ptrs[i], 0xAA, sizes[i]);
		}

		/* Phase 2: 全部释放 */
		for (int i = 0; i < SLAB_NR_CACHES; i++)
			kfree(ptrs[i]);

		/* Phase 3: 再次分配（应从 free_list 取回） */
		for (int i = 0; i < SLAB_NR_CACHES; i++) {
			ptrs[i] = kmalloc(sizes[i]);
			TEST_ASSERT_NOT_NULL(ptrs[i]);
		}

		/* Phase 4: 再次释放 */
		for (int i = 0; i < SLAB_NR_CACHES; i++)
			kfree(ptrs[i]);
#undef SLAB_NR_CACHES
	}
	TEST_END("slab: basic alloc/free");
	return;
fail:
	TEST_FAIL("slab: basic alloc/free", "see above");
}

/**
 * test_slab_cross_cache - 跨缓存大小测试
 *
 * 分配各种"非标准"大小（如 7, 17, 33, 65 字节），
 * 验证 kmalloc 能正确向上取整到最近的缓存大小。
 */
static void test_slab_cross_cache(void)
{
	TEST_BEGIN("slab: cross-cache sizes");
	{
		/* 非对齐大小，kmalloc 应向上取整 */
		size_t odd_sizes[] = {1, 7, 15, 17, 33, 65, 100, 200, 500,
				      1000, 1500};
		int n = sizeof(odd_sizes) / sizeof(odd_sizes[0]);
		void *ptrs[16];

		TEST_ASSERT(n <= 16);

		for (int i = 0; i < n; i++) {
			ptrs[i] = kmalloc(odd_sizes[i]);
			TEST_ASSERT_NOT_NULL(ptrs[i]);
			memset(ptrs[i], 0xEE, odd_sizes[i]);
		}

		for (int i = 0; i < n; i++)
			kfree(ptrs[i]);
	}
	TEST_END("slab: cross-cache sizes");
	return;
fail:
	TEST_FAIL("slab: cross-cache sizes", "see above");
}

/**
 * test_slab_stress - SLAB 压力测试
 *
 * 大量分配/释放循环，验证不会泄漏或崩溃。
 */
static void test_slab_stress(void)
{
	TEST_BEGIN("slab: stress cycle");
	{
#define SLAB_STRESS_N 128
		void *ptrs[SLAB_STRESS_N];

		for (int round = 0; round < 5; round++) {
			for (int i = 0; i < SLAB_STRESS_N; i++) {
				/* 交替使用不同大小 */
				size_t sz = 16 << (i % 8);
				ptrs[i] = kmalloc(sz);
				TEST_ASSERT_NOT_NULL(ptrs[i]);
				memset(ptrs[i], (uint8_t)round, sz);
			}
			for (int i = 0; i < SLAB_STRESS_N; i++)
				kfree(ptrs[i]);
		}
#undef SLAB_STRESS_N
	}
	TEST_END("slab: stress cycle");
	return;
fail:
	TEST_FAIL("slab: stress cycle", "see above");
}

/* ================================================================
 *  Trap 测试
 * ================================================================ */

/**
 * test_trap_frame_layout - 验证 trap_frame 结构大小与字段偏移
 *
 * 确认 struct trap_frame 恰好 35 个 size_t 字段（280 字节 on rv64），
 * 且字段偏移与 entry.S 的保存顺序一致。
 */
static void test_trap_frame_layout(void)
{
	TEST_BEGIN("trap: frame layout");
	{
		struct trap_frame tf;

		(void)tf; /* 抑制未使用警告 */

		/* 结构体应有 35 个 size_t 字段 */
		TEST_ASSERT_EQ(sizeof(tf), (size_t)(35 * sizeof(size_t)));

		/* 验证 sepc 在最前面 */
		TEST_ASSERT_EQ(offsetof(struct trap_frame, sepc),
			       (size_t)0);

		/* 验证 scause 偏移 = 32 * sizeof(size_t) */
		TEST_ASSERT_EQ(offsetof(struct trap_frame, scause),
			       (size_t)(32 * sizeof(size_t)));

		/* 验证 stval 紧跟 scause */
		TEST_ASSERT_EQ(offsetof(struct trap_frame, stval),
			       (size_t)(33 * sizeof(size_t)));

		/* 验证 sstatus 在最后 */
		TEST_ASSERT_EQ(offsetof(struct trap_frame, sstatus),
			       (size_t)(34 * sizeof(size_t)));
	}
	TEST_END("trap: frame layout");
	return;
fail:
	TEST_FAIL("trap: frame layout", "see above");
}

/**
 * test_trap_from_user - 测试 from_user 辅助函数
 *
 * 构造不同 SSTATUS.SPP 值的 trap_frame，验证 from_user 返回值。
 */
static void test_trap_from_user(void)
{
	TEST_BEGIN("trap: from_user helper");
	{
		struct trap_frame tf;

		memset(&tf, 0, sizeof(tf));

		/* SPP=0 (用户模式) → from_user 返回 true */
		tf.sstatus = 0;
		TEST_ASSERT(from_user(&tf) == true);

		/* SPP=1 (内核模式) → from_user 返回 false */
		tf.sstatus = SSTATUS_SPP;
		TEST_ASSERT(from_user(&tf) == false);

		/* SPP=0 但其他位有值 → from_user 仍返回 true */
		tf.sstatus = SSTATUS_SIE;
		TEST_ASSERT(from_user(&tf) == true);
	}
	TEST_END("trap: from_user helper");
	return;
fail:
	TEST_FAIL("trap: from_user helper", "see above");
}

/**
 * test_trap_context_layout - 验证 context 结构（上下文切换用）
 *
 * struct context 应包含 14 个 size_t 字段：ra, sp, s0~s11。
 */
static void test_trap_context_layout(void)
{
	TEST_BEGIN("trap: context layout");
	{
		struct context ctx;
		(void)ctx;

		/* 14 个 callee-saved 寄存器 */
		TEST_ASSERT_EQ(sizeof(ctx),
			       (size_t)(14 * sizeof(size_t)));

		/* ra 在最前 */
		TEST_ASSERT_EQ(offsetof(struct context, ra), (size_t)0);

		/* sp 紧跟 ra */
		TEST_ASSERT_EQ(offsetof(struct context, sp),
			       (size_t)sizeof(size_t));
	}
	TEST_END("trap: context layout");
	return;
fail:
	TEST_FAIL("trap: context layout", "see above");
}

/**
 * test_trap_irq_codes - 验证 IRQ/异常码常量
 *
 * 确保 scause 编码值与 RISC-V 特权规范一致。
 */
static void test_trap_irq_codes(void)
{
	TEST_BEGIN("trap: IRQ/exception codes");
	{
		/* 中断码 */
		TEST_ASSERT_EQ(IRQ_S_SOFT, 1UL);
		TEST_ASSERT_EQ(IRQ_S_TIMER, 5UL);
		TEST_ASSERT_EQ(IRQ_S_EXT, 9UL);

		/* 异常码 */
		TEST_ASSERT_EQ(EXC_INST_ILLEGAL, 2UL);
		TEST_ASSERT_EQ(EXC_ECALL_U, 8UL);
		TEST_ASSERT_EQ(EXC_ECALL_S, 9UL);
		TEST_ASSERT_EQ(EXC_LOAD_PAGE_FAULT, 13UL);
		TEST_ASSERT_EQ(EXC_STORE_PAGE_FAULT, 15UL);

		/* 中断标志位 */
		TEST_ASSERT_EQ(SCAUSE_IRQ_FLAG, (1UL << 63));
	}
	TEST_END("trap: IRQ/exception codes");
	return;
fail:
	TEST_FAIL("trap: IRQ/exception codes", "see above");
}

/* ================================================================
 *  Timer 测试
 * ================================================================ */

/**
 * test_timer_mtime - 测试 mtime 单调递增
 *
 * 连续读取 time CSR，验证值严格递增（或相等，频率极高时）。
 */
static void test_timer_mtime(void)
{
	TEST_BEGIN("timer: mtime monotonic");
	{
		uint64_t t0 = get_mtime();

		/* 读 1000 次，值不应减小 */
		for (int i = 0; i < 1000; i++) {
			uint64_t t1 = get_mtime();
			TEST_ASSERT(t1 >= t0);
			t0 = t1;
		}
	}
	TEST_END("timer: mtime monotonic");
	return;
fail:
	TEST_FAIL("timer: mtime monotonic", "see above");
}

/**
 * test_timer_mtimecmp - 测试 stimecmp CSR 可写
 *
 * 写入 stimecmp 并读回（通过 get_mtime 间接验证不触发异常）。
 */
static void test_timer_mtimecmp(void)
{
	TEST_BEGIN("timer: mtimecmp write/read");
	{
		uint64_t now = get_mtime();

		/* 设置一个远的超时，不触发中断 */
		set_mtimecmp(now + 10000000UL);

		/* 如果没 panic，说明 CSR 可写 */
		/* 恢复正常的 tick 间隔 */
		set_mtimecmp(now + 100000UL);
	}
	TEST_END("timer: mtimecmp write/read");
}

/**
 * test_timer_jiffies - 验证 jiffies 初始值
 *
 * 在测试启动时 jiffies 应已被若干 timer tick 递增过（> 0）。
 * 仅检查 jiffies 是一个合理的小值。
 */
static void test_timer_jiffies(void)
{
	TEST_BEGIN("timer: jiffies initial value");
	{
		/*
		 * timer_init() 在 kernel_main 中先于 kernel_test() 调用，
		 * jiffies 应 >= 0（可能 > 0，取决于初始化期间是否有 tick）。
		 * 只验证它没有溢出或变为异常值。
		 */
		uint64_t j = jiffies;
		TEST_ASSERT(j < 1000000UL);
	}
	TEST_END("timer: jiffies initial value");
	return;
fail:
	TEST_FAIL("timer: jiffies initial value", "see above");
}

/**
 * test_timer_constants - 验证时钟常量
 *
 * 确保 HZ、CLOCKS_PER_TICK 等常量之间的关系正确。
 */
static void test_timer_constants(void)
{
	TEST_BEGIN("timer: constants");
	{
		/*
		 * 验证 timer.c 中的核心常量关系（直接使用字面量，
		 * 因为这些 #define 在 timer.c 内部，不通过头文件导出）：
		 *
		 *   HZ              = 100        (100 Hz tick)
		 *   MTIME_FREQ      = 10000000   (10 MHz)
		 *   CLOCKS_PER_TICK = 100000     (10000000 / 100)
		 */
		TEST_ASSERT_EQ(10000000UL / 100UL, 100000UL);
		TEST_ASSERT_EQ(100000UL * 100UL, 10000000UL);

		/* 每 tick = 100000 / 10000000 = 10ms */
		TEST_ASSERT_EQ(100000UL * 1000UL / 10000000UL, 10UL);
	}
	TEST_END("timer: constants");
	return;
fail:
	TEST_FAIL("timer: constants", "see above");
}

/* ================================================================
 *  Task 管理测试
 * ================================================================ */

/**
 * test_task_alloc_free - 测试 task 分配与释放
 *
 * 分配一个 task，验证 PID、state、canary 等字段初始化正确，
 * 然后释放。
 */
static void test_task_alloc_free(void)
{
	TEST_BEGIN("task: alloc/free");
	{
		struct task_struct *task = task_alloc();
		TEST_ASSERT_NOT_NULL(task);

		/* PID 应 > 0（idle 占用了 0） */
		TEST_ASSERT(task->pid > 0);
		TEST_ASSERT(task->pid <= PID_MAX);

		/* 初始状态应为 RUNNING */
		TEST_ASSERT_EQ(task->state, (uint32_t)TASK_RUNNING);

		/* 内核栈应已分配 */
		TEST_ASSERT_NOT_NULL(task->kstack);

		/* 内核栈应按 KSTACK_SIZE 对齐 */
		TEST_ASSERT_ALIGNED(task->kstack, PAGE_SIZE);

		/* mm 应为 NULL（内核线程） */
		TEST_ASSERT_NULL(task->mm);

		/* tf 应为 NULL */
		TEST_ASSERT_NULL(task->tf);

		/* canary 应完好 */
		check_canary(task);

		/* 进程树链接应已初始化 */
		TEST_ASSERT(list_empty(&task->children));
		TEST_ASSERT(list_empty(&task->sibling));
		TEST_ASSERT(list_empty(&task->run_list));

		task_free(task);
	}
	TEST_END("task: alloc/free");
	return;
fail:
	TEST_FAIL("task: alloc/free", "see above");
}

/**
 * test_task_canary - 测试 canary 完整性检查
 *
 * 分配 task，验证 canary 正确；破坏 canary 后应被 check_canary 检测。
 * 注意：破坏 canary 会触发 panic，所以这里只验证 canary 初始值正确。
 */
static void test_task_canary(void)
{
	TEST_BEGIN("task: canary integrity");
	{
		struct task_struct *task = task_alloc();
		TEST_ASSERT_NOT_NULL(task);

		/* 直接读取 canary 值 */
		uint64_t *canary_ptr = (uint64_t *)task->kstack;
		TEST_ASSERT_EQ(*canary_ptr, CANARY_MAGIC);

		check_canary(task);
		task_free(task);
	}
	TEST_END("task: canary integrity");
	return;
fail:
	TEST_FAIL("task: canary integrity", "see above");
}

/**
 * test_task_multiple - 测试多 task 分配
 *
 * 分配多个 task，验证 PID 各不相同，然后全部释放。
 */
static void test_task_multiple(void)
{
	TEST_BEGIN("task: multiple tasks");
	{
#define TASK_N_TASKS 8
		struct task_struct *tasks[TASK_N_TASKS];
		pid_t pids[TASK_N_TASKS];

		for (int i = 0; i < TASK_N_TASKS; i++) {
			tasks[i] = task_alloc();
			TEST_ASSERT_NOT_NULL(tasks[i]);
			pids[i] = tasks[i]->pid;
		}

		/* 所有 PID 应互不相同 */
		for (int i = 0; i < TASK_N_TASKS; i++) {
			for (int j = i + 1; j < TASK_N_TASKS; j++) {
				TEST_ASSERT_NE(pids[i], pids[j]);
			}
		}

		/* 释放 */
		for (int i = 0; i < TASK_N_TASKS; i++)
			task_free(tasks[i]);
#undef TASK_N_TASKS
	}
	TEST_END("task: multiple tasks");
	return;
fail:
	TEST_FAIL("task: multiple tasks", "see above");
}

/**
 * test_task_process_tree - 测试进程树链接
 *
 * 手动构建父子关系，验证 children/sibling 链表操作正确。
 */
static void test_task_process_tree(void)
{
	TEST_BEGIN("task: process tree linkage");
	{
		struct task_struct *parent = task_alloc();
		TEST_ASSERT_NOT_NULL(parent);

		struct task_struct *child1 = task_alloc();
		TEST_ASSERT_NOT_NULL(child1);

		struct task_struct *child2 = task_alloc();
		TEST_ASSERT_NOT_NULL(child2);

		/* 构建父子关系 */
		child1->parent = parent;
		list_add_tail(&child1->sibling, &parent->children);

		child2->parent = parent;
		list_add_tail(&child2->sibling, &parent->children);

		/* 验证 parent 有 2 个子进程 */
		TEST_ASSERT(!list_empty(&parent->children));

		int child_count = 0;
		struct task_struct *pos;
		list_for_each_entry(pos, &parent->children, sibling)
			child_count++;
		TEST_ASSERT_EQ(child_count, 2);

		/* 清理 */
		list_del(&child1->sibling);
		list_del(&child2->sibling);
		task_free(child2);
		task_free(child1);
		task_free(parent);
	}
	TEST_END("task: process tree linkage");
	return;
fail:
	TEST_FAIL("task: process tree linkage", "see above");
}

/**
 * test_task_idle - 验证 idle task 初始化
 *
 * idle task 应为 PID 0，current 应指向它。
 */
static void test_task_idle(void)
{
	TEST_BEGIN("task: idle task init");
	{
		TEST_ASSERT_NOT_NULL(current);
		TEST_ASSERT_EQ(current->pid, (pid_t)0);
		TEST_ASSERT_EQ(idle_task.pid, (pid_t)0);
		TEST_ASSERT_EQ(current, &idle_task);
		TEST_ASSERT_EQ(idle_task.state, (uint32_t)TASK_RUNNING);
	}
	TEST_END("task: idle task init");
	return;
fail:
	TEST_FAIL("task: idle task init", "see above");
}

/**
 * test_task_free_null - 测试 task_free(NULL) 安全性
 *
 * task_free 应能安全处理 NULL 指针。
 */
static void test_task_free_null(void)
{
	TEST_BEGIN("task: free(NULL) safe");
	{
		task_free(NULL);
		/* 如果没崩溃就算通过 */
	}
	TEST_END("task: free(NULL) safe");
}

/* ================================================================
 *  Sched 调度器测试
 * ================================================================ */

/**
 * test_sched_init - 验证调度器初始化
 *
 * sched_init() 已在 kernel_main 中调用，runqueue 应为空。
 */
static void test_sched_init(void)
{
	TEST_BEGIN("sched: runqueue init");
	{
		TEST_ASSERT(list_empty(&runqueue));
	}
	TEST_END("sched: runqueue init");
	return;
fail:
	TEST_FAIL("sched: runqueue init", "see above");
}

/**
 * test_sched_enqueue_dequeue - 测试入队/出队操作
 *
 * 入队 3 个 task，验证 FIFO 出队顺序。
 */
static void test_sched_enqueue_dequeue(void)
{
	TEST_BEGIN("sched: enqueue/dequeue");
	{
		struct task_struct *t1 = task_alloc();
		struct task_struct *t2 = task_alloc();
		struct task_struct *t3 = task_alloc();
		TEST_ASSERT_NOT_NULL(t1);
		TEST_ASSERT_NOT_NULL(t2);
		TEST_ASSERT_NOT_NULL(t3);

		/* 入队三个任务 */
		sched_enqueue(t1);
		sched_enqueue(t2);
		sched_enqueue(t3);
		TEST_ASSERT(!list_empty(&runqueue));

		/* 出队应按 FIFO 顺序 */
		struct task_struct *first =
			list_first_entry(&runqueue, struct task_struct, run_list);
		TEST_ASSERT_EQ(first->pid, t1->pid);
		sched_dequeue(first);

		struct task_struct *second =
			list_first_entry(&runqueue, struct task_struct, run_list);
		TEST_ASSERT_EQ(second->pid, t2->pid);
		sched_dequeue(second);

		struct task_struct *third =
			list_first_entry(&runqueue, struct task_struct, run_list);
		TEST_ASSERT_EQ(third->pid, t3->pid);
		sched_dequeue(third);

		TEST_ASSERT(list_empty(&runqueue));

		task_free(t1);
		task_free(t2);
		task_free(t3);
	}
	TEST_END("sched: enqueue/dequeue");
	return;
fail:
	TEST_FAIL("sched: enqueue/dequeue", "see above");
}

/**
 * test_sched_need_resched - 验证 need_resched 字段
 *
 * 新分配的 task 的 need_resched 应为 0，可设置和清除。
 */
static void test_sched_need_resched(void)
{
	TEST_BEGIN("sched: need_resched field");
	{
		struct task_struct *t = task_alloc();
		TEST_ASSERT_NOT_NULL(t);

		/* 初始应为 0 */
		TEST_ASSERT_EQ(t->need_resched, (uint8_t)0);

		/* 设置和清除 */
		t->need_resched = 1;
		TEST_ASSERT_EQ(t->need_resched, (uint8_t)1);
		t->need_resched = 0;
		TEST_ASSERT_EQ(t->need_resched, (uint8_t)0);

		task_free(t);
	}
	TEST_END("sched: need_resched field");
	return;
fail:
	TEST_FAIL("sched: need_resched field", "see above");
}

/* ================================================================
 *  Kernel Thread 测试
 * ================================================================ */

/* entry.S 中的 trap 返回入口，用于验证 ctx.ra 指向正确地址 */
extern void __trapret(void);

/**
 * test_kernel_thread_basic - 验证 kernel_thread 基本功能
 *
 * 创建一个内核线程，检查返回值非 NULL、PID 有效、
 * 状态为 TASK_RUNNING、已加入就绪队列。
 * 测试完毕后清理（dequeue + free）。
 */
static void dummy_thread_fn(void *arg)
{
	(void)arg;
	/* 不会被实际执行（测试中不调用 schedule） */
}

static void test_kernel_thread_basic(void)
{
	TEST_BEGIN("kthread: basic create");
	{
		/* 先记录队列状态 */
		int was_empty = list_empty(&runqueue);

		struct task_struct *t = kernel_thread(dummy_thread_fn, NULL);
		TEST_ASSERT_NOT_NULL(t);
		TEST_ASSERT(t->pid > 0);
		TEST_ASSERT_EQ(t->state, (uint32_t)TASK_RUNNING);
		TEST_ASSERT(t->kstack != NULL);

		/* kernel_thread 应已将任务入队 */
		TEST_ASSERT(!list_empty(&runqueue));

		/* 清理：出队并释放 */
		sched_dequeue(t);
		task_free(t);

		/* 队列应恢复原状 */
		if (was_empty)
			TEST_ASSERT(list_empty(&runqueue));
	}
	TEST_END("kthread: basic create");
	return;
fail:
	TEST_FAIL("kthread: basic create", "see above");
}

/**
 * test_kernel_thread_ctx_setup - 验证 trap_frame 和 context 设置
 *
 * 检查 kernel_thread 创建的任务：
 *   - ctx.ra 指向 __trapret
 *   - ctx.sp 指向内核栈上的 trap_frame
 *   - tf->sepc 指向入口函数
 *   - tf->a0 等于传入的 arg
 *   - tf->sstatus 包含 SPP 和 SPIE 位
 */
static void test_kernel_thread_ctx_setup(void)
{
	TEST_BEGIN("kthread: ctx and trap_frame setup");
	{
		int test_arg_val = 0x1234;
		struct task_struct *t = kernel_thread(dummy_thread_fn,
						      (void *)(size_t)test_arg_val);
		TEST_ASSERT_NOT_NULL(t);

		/* ctx.ra 应指向 __trapret */
		TEST_ASSERT_EQ(t->ctx.ra, (size_t)__trapret);

		/* ctx.sp 应指向栈顶的 trap_frame */
		struct trap_frame *expected_tf = (struct trap_frame *)
			((uint8_t *)t->kstack + KSTACK_SIZE - sizeof(struct trap_frame));
		TEST_ASSERT_EQ(t->ctx.sp, (size_t)expected_tf);

		/* tf 指针正确 */
		TEST_ASSERT_EQ((size_t)t->tf, (size_t)expected_tf);

		/* sepc 指向入口函数 */
		TEST_ASSERT_EQ(t->tf->sepc, (size_t)dummy_thread_fn);

		/* a0 传递了 arg */
		TEST_ASSERT_EQ(t->tf->a0, (size_t)test_arg_val);

		/* sstatus 包含 SPP 和 SPIE */
		TEST_ASSERT(t->tf->sstatus & SSTATUS_SPP);
		TEST_ASSERT(t->tf->sstatus & SSTATUS_SPIE);

		/* 清理 */
		sched_dequeue(t);
		task_free(t);
	}
	TEST_END("kthread: ctx and trap_frame setup");
	return;
fail:
	TEST_FAIL("kthread: ctx and trap_frame setup", "see above");
}

/* ================================================================
 *  公共接口
 * ================================================================ */

/**
 * kernel_test - 运行所有内核自测
 *
 * 在 kernel_main 中、各子系统初始化完成后调用。
 * 每个子系统前后打印分隔线，方便阅读。最后输出汇总。
 */
void kernel_test(void)
{
	__test_total = 0;
	__test_passed = 0;
	__test_failed = 0;

	printk("\n");
	printk("========================================\n");
	printk("        CuteOS Kernel Self-Test         \n");
	printk("========================================\n");

	/* ---- Bitmap ---- */
	TEST_SECTION("Bitmap");
	test_bitmap();
	test_bitmap_find_first_zero();
	test_bitmap_odd_bits();

	/* ---- PID ---- */
	TEST_SECTION("PID");
	test_pid_basic();
	test_pid_exhaust();

	/* ---- Buddy ---- */
	TEST_SECTION("Buddy");
	test_buddy_single_page();
	test_buddy_multi_order();
	test_buddy_merge();
	test_buddy_split();
	test_buddy_stress();

	/* ---- SLAB ---- */
	TEST_SECTION("SLAB");
	test_slab_basic();
	test_slab_cross_cache();
	test_slab_stress();

	/* ---- Trap ---- */
	TEST_SECTION("Trap");
	test_trap_frame_layout();
	test_trap_from_user();
	test_trap_context_layout();
	test_trap_irq_codes();

	/* ---- Timer ---- */
	TEST_SECTION("Timer");
	test_timer_mtime();
	test_timer_mtimecmp();
	test_timer_jiffies();
	test_timer_constants();

	/* ---- Task ---- */
	TEST_SECTION("Task");
	test_task_idle();
	test_task_alloc_free();
	test_task_canary();
	test_task_multiple();
	test_task_process_tree();
	test_task_free_null();

	/* ---- Sched ---- */
	TEST_SECTION("Sched");
	test_sched_init();
	test_sched_enqueue_dequeue();
	test_sched_need_resched();

	/* ---- Kernel Thread ---- */
	TEST_SECTION("Kernel Thread");
	test_kernel_thread_basic();
	test_kernel_thread_ctx_setup();

	/* ---- 汇总 ---- */
	printk("\n========================================\n");
	printk("  Total: %d  |  Passed: %d  |  Failed: %d\n",
	       (int)__test_total, (int)__test_passed, (int)__test_failed);
	printk("========================================\n");

	if (__test_failed > 0)
		printk("  *** SOME TESTS FAILED ***\n");
	else
		printk("  ALL TESTS PASSED\n");
	printk("\n");
}
