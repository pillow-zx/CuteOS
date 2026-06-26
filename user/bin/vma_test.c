/*
 * user/bin/vma_test.c - VMA/内存映射语义测试
 *
 * 测试内容：
 *   1. brk 增长堆：验证新页零初始化并可写
 *   2. mmap 匿名私有：写入后读回，验证数据完整
 *   3. munmap 完整区域：解除后不可再访问（通过重新映射验证）
 *   4. munmap 中间区域：触发 VMA 分裂，两端仍可访问
 *   5. mmap 多次分配：确保各映射相互独立
 */

#include <ulib.h>
#include <uapi/mman.h>

#define PAGE_SIZE 4096UL

/* ---- test 1: brk growth ---- */

static int test_brk_growth(void)
{
	long old_brk = brk(0);
	long new_brk;
	char *p;

	if (old_brk <= 0) {
		printf("FAIL: brk(0) returned %ld\n", old_brk);
		return 1;
	}

	/* Grow heap by one page. */
	new_brk = brk(old_brk + (long)PAGE_SIZE);
	if (new_brk != old_brk + (long)PAGE_SIZE) {
		printf("FAIL: brk grow: expected %ld got %ld\n",
		       old_brk + (long)PAGE_SIZE, new_brk);
		return 1;
	}

	/* New page must be zero-initialized. */
	p = (char *)old_brk;
	for (size_t i = 0; i < PAGE_SIZE; i++) {
		if (p[i] != 0) {
			printf("FAIL: brk page not zero at offset %zu\n", i);
			brk(old_brk);
			return 1;
		}
	}

	/* Write a pattern and read it back. */
	for (size_t i = 0; i < PAGE_SIZE; i++)
		p[i] = (char)(i & 0xff);
	for (size_t i = 0; i < PAGE_SIZE; i++) {
		if (p[i] != (char)(i & 0xff)) {
			printf("FAIL: brk page data mismatch at %zu\n", i);
			brk(old_brk);
			return 1;
		}
	}

	/* Restore heap top. */
	brk(old_brk);
	return 0;
}

/* ---- test 2: mmap anonymous write/read ---- */

static int test_mmap_anon(void)
{
	char *m;
	const size_t len = 2 * PAGE_SIZE;

	m = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS,
		 -1, 0);
	if ((long)m < 0) {
		printf("FAIL: mmap: %ld\n", (long)m);
		return 1;
	}

	/* Zero-init check. */
	for (size_t i = 0; i < len; i++) {
		if (m[i] != 0) {
			printf("FAIL: mmap not zero at %zu\n", i);
			munmap(m, len);
			return 1;
		}
	}

	/* Write and read back. */
	for (size_t i = 0; i < len; i++)
		m[i] = (char)(i * 7 & 0xff);
	for (size_t i = 0; i < len; i++) {
		if (m[i] != (char)(i * 7 & 0xff)) {
			printf("FAIL: mmap data mismatch at %zu\n", i);
			munmap(m, len);
			return 1;
		}
	}

	munmap(m, len);
	return 0;
}

/* ---- test 3: VMA split via middle munmap ---- */

static int test_munmap_split(void)
{
	char *m;
	const size_t len = 4 * PAGE_SIZE;

	m = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS,
		 -1, 0);
	if ((long)m < 0) {
		printf("FAIL: mmap for split: %ld\n", (long)m);
		return 1;
	}

	/* Write sentinel bytes at first and last page. */
	m[0] = 0xAA;
	m[len - 1] = 0xBB;

	/* Unmap the two middle pages. */
	long rc = munmap(m + PAGE_SIZE, 2 * PAGE_SIZE);

	if (rc != 0) {
		printf("FAIL: munmap middle: %ld\n", rc);
		munmap(m, len);
		return 1;
	}

	/* First and last pages should still be accessible. */
	if ((unsigned char)m[0] != 0xAA) {
		printf("FAIL: head sentinel lost after split munmap\n");
		munmap(m, PAGE_SIZE);
		munmap(m + 3 * PAGE_SIZE, PAGE_SIZE);
		return 1;
	}
	if ((unsigned char)m[len - 1] != 0xBB) {
		printf("FAIL: tail sentinel lost after split munmap\n");
		munmap(m, PAGE_SIZE);
		munmap(m + 3 * PAGE_SIZE, PAGE_SIZE);
		return 1;
	}

	/* Clean up the two remaining VMAs. */
	munmap(m, PAGE_SIZE);
	munmap(m + 3 * PAGE_SIZE, PAGE_SIZE);
	return 0;
}

/* ---- test 4: multiple independent mappings ---- */

static int test_mmap_independent(void)
{
#define N_MAPS 4
	char *maps[N_MAPS];
	int failed = 0;

	for (int i = 0; i < N_MAPS; i++) {
		maps[i] = mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE,
			       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		if ((long)maps[i] < 0) {
			printf("FAIL: mmap[%d]: %ld\n", i, (long)maps[i]);
			for (int j = 0; j < i; j++)
				munmap(maps[j], PAGE_SIZE);
			return 1;
		}
		maps[i][0] = (char)i;
	}

	/* Verify each mapping has its own data. */
	for (int i = 0; i < N_MAPS; i++) {
		if (maps[i][0] != (char)i) {
			printf("FAIL: map[%d] data corrupted\n", i);
			failed = 1;
		}
	}

	for (int i = 0; i < N_MAPS; i++)
		munmap(maps[i], PAGE_SIZE);

	return failed;
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;

	int failed = 0;

	printf("vma_test: brk growth ... ");
	if (test_brk_growth())
		failed++;
	else
		printf("PASS\n");

	printf("vma_test: mmap anonymous ... ");
	if (test_mmap_anon())
		failed++;
	else
		printf("PASS\n");

	printf("vma_test: munmap middle split ... ");
	if (test_munmap_split())
		failed++;
	else
		printf("PASS\n");

	printf("vma_test: multiple independent mappings ... ");
	if (test_mmap_independent())
		failed++;
	else
		printf("PASS\n");

	if (failed)
		printf("vma_test: %d test(s) FAILED\n", failed);
	else
		printf("vma_test: all tests PASSED\n");

	return failed ? 1 : 0;
}
