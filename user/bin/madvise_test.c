/*
 * user/bin/madvise_test.c - madvise 和 mincore 测试
 *
 * 测试内容：
 *   madvise:
 *     1. MADV_DONTNEED 使用 Linux ABI 值 4
 *     2. MADV_DONTNEED 在有效 VMA 范围内返回 0，并丢弃匿名页内容
 *     3. MADV_NORMAL/RANDOM/SEQUENTIAL/WILLNEED 返回 0
 *     4. 未对齐地址返回 -EINVAL
 *     5. 未知 advice 返回 -EINVAL
 *   mincore:
 *     6. mmap 两页，写第 0 页（触发缺页），第 1 页不写
 *     7. mincore 后 vec[0] bit0 = 1（已驻留），vec[1] bit0 = 0（未驻留）
 *     8. 未对齐地址返回 -EINVAL
 *     9. len == 0 返回 0
 *     10. PROT_NONE 页仍报告 resident
 */

#include <ulib.h>
#include <uapi/mman.h>

#define PAGE_SIZE 4096UL

/* ---- madvise tests ---- */

static int test_madvise_valid(void)
{
	char *m;
	long ret;

	if (MADV_DONTNEED != 4) {
		printf("FAIL: MADV_DONTNEED ABI value is %d, want 4\n",
		       MADV_DONTNEED);
		return 1;
	}

	m = mmap(NULL, 2 * PAGE_SIZE, PROT_READ | PROT_WRITE,
		 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if ((long)m < 0) {
		printf("FAIL: mmap: %ld\n", (long)m);
		return 1;
	}

	/* Touch pages so they exist. */
	m[0] = 1;
	m[PAGE_SIZE] = 2;

	ret = madvise(m, 2 * PAGE_SIZE, MADV_DONTNEED);
	if (ret != 0) {
		printf("FAIL: MADV_DONTNEED: %ld\n", ret);
		munmap(m, 2 * PAGE_SIZE);
		return 1;
	}
	if (m[0] != 0 || m[PAGE_SIZE] != 0) {
		printf("FAIL: MADV_DONTNEED did not zero-fill pages\n");
		munmap(m, 2 * PAGE_SIZE);
		return 1;
	}

	ret = madvise(m, PAGE_SIZE, MADV_REMOVE);
	if (ret != -22) { /* -EINVAL */
		printf("FAIL: MADV_REMOVE should be rejected, got %ld\n", ret);
		munmap(m, 2 * PAGE_SIZE);
		return 1;
	}

	ret = madvise(m, PAGE_SIZE, MADV_NORMAL);
	if (ret != 0) {
		printf("FAIL: MADV_NORMAL: %ld\n", ret);
		munmap(m, 2 * PAGE_SIZE);
		return 1;
	}

	ret = madvise(m, PAGE_SIZE, MADV_RANDOM);
	if (ret != 0) {
		printf("FAIL: MADV_RANDOM: %ld\n", ret);
		munmap(m, 2 * PAGE_SIZE);
		return 1;
	}

	ret = madvise(m, PAGE_SIZE, MADV_SEQUENTIAL);
	if (ret != 0) {
		printf("FAIL: MADV_SEQUENTIAL: %ld\n", ret);
		munmap(m, 2 * PAGE_SIZE);
		return 1;
	}

	ret = madvise(m, PAGE_SIZE, MADV_WILLNEED);
	if (ret != 0) {
		printf("FAIL: MADV_WILLNEED: %ld\n", ret);
		munmap(m, 2 * PAGE_SIZE);
		return 1;
	}

	munmap(m, 2 * PAGE_SIZE);
	return 0;
}

static int test_madvise_unaligned(void)
{
	char *m;
	long ret;

	m = mmap(NULL, 2 * PAGE_SIZE, PROT_READ | PROT_WRITE,
		 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if ((long)m < 0) {
		printf("FAIL: mmap: %ld\n", (long)m);
		return 1;
	}

	ret = madvise(m + 1, PAGE_SIZE, MADV_DONTNEED);
	if (ret != -22) { /* -EINVAL */
		printf("FAIL: unaligned addr: expected -22 got %ld\n", ret);
		munmap(m, 2 * PAGE_SIZE);
		return 1;
	}

	munmap(m, 2 * PAGE_SIZE);
	return 0;
}

static int test_madvise_unknown_advice(void)
{
	char *m;
	long ret;

	m = mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE,
		 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if ((long)m < 0) {
		printf("FAIL: mmap: %ld\n", (long)m);
		return 1;
	}

	ret = madvise(m, PAGE_SIZE, 0x7fff);
	if (ret != -22) { /* -EINVAL */
		printf("FAIL: unknown advice: expected -22 got %ld\n", ret);
		munmap(m, PAGE_SIZE);
		return 1;
	}

	munmap(m, PAGE_SIZE);
	return 0;
}

/* ---- mincore tests ---- */

static int test_mincore_basic(void)
{
	char *m;
	unsigned char vec[2];
	long ret;

	m = mmap(NULL, 2 * PAGE_SIZE, PROT_READ | PROT_WRITE,
		 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if ((long)m < 0) {
		printf("FAIL: mmap: %ld\n", (long)m);
		return 1;
	}

	/* Write only the first page to trigger page fault (map it). */
	m[0] = 42;
	/* Leave second page untouched. */

	vec[0] = 0;
	vec[1] = 0;
	ret = mincore(m, 2 * PAGE_SIZE, vec);
	if (ret != 0) {
		printf("FAIL: mincore returned %ld\n", ret);
		munmap(m, 2 * PAGE_SIZE);
		return 1;
	}

	if (!(vec[0] & 1)) {
		printf("FAIL: page 0 should be resident (vec[0]=%u)\n", vec[0]);
		munmap(m, 2 * PAGE_SIZE);
		return 1;
	}

	if (vec[1] & 1) {
		printf("FAIL: page 1 should not be resident (vec[1]=%u)\n",
		       vec[1]);
		munmap(m, 2 * PAGE_SIZE);
		return 1;
	}

	munmap(m, 2 * PAGE_SIZE);
	return 0;
}

static int test_mincore_unaligned(void)
{
	char *m;
	unsigned char vec[1];
	long ret;

	m = mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE,
		 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if ((long)m < 0) {
		printf("FAIL: mmap: %ld\n", (long)m);
		return 1;
	}

	ret = mincore(m + 1, PAGE_SIZE, vec);
	if (ret != -22) { /* -EINVAL */
		printf("FAIL: mincore unaligned: expected -22 got %ld\n", ret);
		munmap(m, PAGE_SIZE);
		return 1;
	}

	munmap(m, PAGE_SIZE);
	return 0;
}

static int test_mincore_zero_len(void)
{
	char *m;
	long ret;

	m = mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE,
		 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if ((long)m < 0) {
		printf("FAIL: mmap: %ld\n", (long)m);
		return 1;
	}

	ret = mincore(m, 0, NULL);
	if (ret != 0) {
		printf("FAIL: mincore len=0 expected 0 got %ld\n", ret);
		munmap(m, PAGE_SIZE);
		return 1;
	}

	munmap(m, PAGE_SIZE);
	return 0;
}

static int test_mincore_prot_none_resident(void)
{
	char *m;
	unsigned char vec[1];
	long ret;

	m = mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE,
		 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if ((long)m < 0) {
		printf("FAIL: mmap: %ld\n", (long)m);
		return 1;
	}

	m[0] = 0x5a;
	ret = mprotect(m, PAGE_SIZE, PROT_NONE);
	if (ret != 0) {
		printf("FAIL: mprotect PROT_NONE: %ld\n", ret);
		munmap(m, PAGE_SIZE);
		return 1;
	}

	vec[0] = 0;
	ret = mincore(m, PAGE_SIZE, vec);
	if (ret != 0 || !(vec[0] & 1)) {
		printf("FAIL: mincore PROT_NONE ret=%ld vec=%u\n", ret, vec[0]);
		mprotect(m, PAGE_SIZE, PROT_READ | PROT_WRITE);
		munmap(m, PAGE_SIZE);
		return 1;
	}

	ret = mprotect(m, PAGE_SIZE, PROT_READ | PROT_WRITE);
	if (ret != 0 || m[0] != 0x5a) {
		printf("FAIL: PROT_NONE restore ret=%ld value=0x%x\n",
		       ret, (unsigned char)m[0]);
		munmap(m, PAGE_SIZE);
		return 1;
	}

	munmap(m, PAGE_SIZE);
	return 0;
}

int main(void)
{
	int fail = 0;

	fail += test_madvise_valid();
	fail += test_madvise_unaligned();
	fail += test_madvise_unknown_advice();
	fail += test_mincore_basic();
	fail += test_mincore_unaligned();
	fail += test_mincore_zero_len();
	fail += test_mincore_prot_none_resident();

	if (fail == 0)
		printf("madvise_test: PASS\n");
	else
		printf("madvise_test: FAIL (%d)\n", fail);

	return fail ? 1 : 0;
}
