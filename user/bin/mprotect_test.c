/*
 * user/bin/mprotect_test.c - mprotect 测试
 *
 * 测试内容：
 *   1. mprotect PROT_READ → 写入触发 SIGSEGV（handler 捕获），再
 *      mprotect 回 PROT_READ|PROT_WRITE → 写入成功
 *   2. mprotect 覆盖范围子集（只改中间页）
 *   3. 未对齐地址返回 -EINVAL
 *   4. 非 VMA 范围返回 -ENOMEM
 */

#include <ulib.h>
#include <uapi/mman.h>
#include <uapi/signal.h>

#define PAGE_SIZE 4096UL

/* test 1: write to mprotect(PROT_READ) triggers SIGSEGV,
 *         then mprotect back to RW allows writes */
static int test_ro_rw(void)
{
	char *m;
	long ret;
	volatile char *p;
	long pid;
	int status;

	m = mmap(NULL, 2 * PAGE_SIZE, PROT_READ | PROT_WRITE,
		 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if ((long)m < 0) {
		printf("FAIL: mmap: %ld\n", (long)m);
		return 1;
	}

	/* Touch both pages. */
	m[0] = 0xab;
	m[PAGE_SIZE] = 0xcd;

	/* Remove write on first page. */
	ret = mprotect(m, PAGE_SIZE, PROT_READ);
	if (ret != 0) {
		printf("FAIL: mprotect PROT_READ: %ld\n", ret);
		munmap(m, 2 * PAGE_SIZE);
		return 1;
	}

	/* Writing to read-only page must fault. */
	p = (volatile char *)m;
	pid = fork();
	if (pid == 0) {
		*p = 0x24;
		exit(7);
	}
	if (pid < 0) {
		printf("FAIL: fork: %ld\n", pid);
		munmap(m, 2 * PAGE_SIZE);
		return 1;
	}
	if (wait4(pid, &status, 0, NULL) != pid) {
		printf("FAIL: wait4 child\n");
		munmap(m, 2 * PAGE_SIZE);
		return 1;
	}
	if (status != (SIGNAL_EXIT_CODE(SIGSEGV) << 8)) {
		printf("FAIL: write to RO page status=0x%x\n", status);
		munmap(m, 2 * PAGE_SIZE);
		return 1;
	}

	/* Restore write permission. */
	ret = mprotect(m, PAGE_SIZE, PROT_READ | PROT_WRITE);
	if (ret != 0) {
		printf("FAIL: mprotect restore RW: %ld\n", ret);
		munmap(m, 2 * PAGE_SIZE);
		return 1;
	}

	/* Write must succeed now. */
	m[0] = 0x42;
	if (m[0] != 0x42) {
		printf("FAIL: write after restore failed\n");
		munmap(m, 2 * PAGE_SIZE);
		return 1;
	}

	/* Second page (never mprotected) must still be writable. */
	m[PAGE_SIZE] = 0x77;
	if (m[PAGE_SIZE] != 0x77) {
		printf("FAIL: second page write failed\n");
		munmap(m, 2 * PAGE_SIZE);
		return 1;
	}

	m[PAGE_SIZE] = 0x55;
	ret = mprotect(m + PAGE_SIZE, PAGE_SIZE, PROT_NONE);
	if (ret != 0) {
		printf("FAIL: mprotect PROT_NONE: %ld\n", ret);
		munmap(m, 2 * PAGE_SIZE);
		return 1;
	}
	pid = fork();
	if (pid == 0) {
		ret = mprotect(m + PAGE_SIZE, PAGE_SIZE,
			       PROT_READ | PROT_WRITE);
		if (ret != 0 || m[PAGE_SIZE] != 0x55)
			exit(9);
		exit(0);
	}
	if (pid < 0) {
		printf("FAIL: fork after PROT_NONE: %ld\n", pid);
		mprotect(m + PAGE_SIZE, PAGE_SIZE, PROT_READ | PROT_WRITE);
		munmap(m, 2 * PAGE_SIZE);
		return 1;
	}
	if (wait4(pid, &status, 0, NULL) != pid) {
		printf("FAIL: wait4 PROT_NONE child\n");
		mprotect(m + PAGE_SIZE, PAGE_SIZE, PROT_READ | PROT_WRITE);
		munmap(m, 2 * PAGE_SIZE);
		return 1;
	}
	if (status != 0) {
		printf("FAIL: PROT_NONE child restore status=0x%x\n", status);
		mprotect(m + PAGE_SIZE, PAGE_SIZE, PROT_READ | PROT_WRITE);
		munmap(m, 2 * PAGE_SIZE);
		return 1;
	}
	ret = mprotect(m + PAGE_SIZE, PAGE_SIZE, PROT_READ | PROT_WRITE);
	if (ret != 0 || m[PAGE_SIZE] != 0x55) {
		printf("FAIL: PROT_NONE restore lost page data ret=%ld\n", ret);
		munmap(m, 2 * PAGE_SIZE);
		return 1;
	}

	munmap(m, 2 * PAGE_SIZE);
	return 0;
}

/* test 2: mprotect a subset range (middle page of 3) */
static int test_partial_range(void)
{
	char *m;
	long ret;

	m = mmap(NULL, 3 * PAGE_SIZE, PROT_READ | PROT_WRITE,
		 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if ((long)m < 0) {
		printf("FAIL: mmap: %ld\n", (long)m);
		return 1;
	}

	/* Touch all pages. */
	m[0]             = 1;
	m[PAGE_SIZE]     = 2;
	m[2 * PAGE_SIZE] = 3;

	/* Protect only the middle page read-only. */
	ret = mprotect(m + PAGE_SIZE, PAGE_SIZE, PROT_READ);
	if (ret != 0) {
		printf("FAIL: mprotect middle: %ld\n", ret);
		munmap(m, 3 * PAGE_SIZE);
		return 1;
	}

	/* First and last pages still writable. */
	m[0]             = 0x11;
	m[2 * PAGE_SIZE] = 0x33;
	if (m[0] != 0x11 || m[2 * PAGE_SIZE] != 0x33) {
		printf("FAIL: adjacent pages not writable\n");
		munmap(m, 3 * PAGE_SIZE);
		return 1;
	}

	/* Restore middle page. */
	ret = mprotect(m + PAGE_SIZE, PAGE_SIZE, PROT_READ | PROT_WRITE);
	if (ret != 0) {
		printf("FAIL: mprotect restore middle: %ld\n", ret);
		munmap(m, 3 * PAGE_SIZE);
		return 1;
	}

	munmap(m, 3 * PAGE_SIZE);
	return 0;
}

/* test 3: unaligned address returns -EINVAL */
static int test_unaligned(void)
{
	char *m;
	long ret;

	m = mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE,
		 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if ((long)m < 0) {
		printf("FAIL: mmap: %ld\n", (long)m);
		return 1;
	}

	ret = mprotect(m + 1, PAGE_SIZE - 1, PROT_READ);
	if (ret != -22) { /* -EINVAL */
		printf("FAIL: unaligned: expected -22 got %ld\n", ret);
		munmap(m, PAGE_SIZE);
		return 1;
	}

	munmap(m, PAGE_SIZE);
	return 0;
}

/* test 4: unmapped range returns -ENOMEM */
static int test_unmapped(void)
{
	long ret = mprotect((void *)0x100000, PAGE_SIZE, PROT_READ);

	if (ret != -12) { /* -ENOMEM */
		printf("FAIL: unmapped: expected -12 got %ld\n", ret);
		return 1;
	}
	return 0;
}

int main(void)
{
	int fail = 0;

	fail += test_ro_rw();
	fail += test_partial_range();
	fail += test_unaligned();
	fail += test_unmapped();

	if (fail == 0)
		printf("mprotect_test: PASS\n");
	else
		printf("mprotect_test: FAIL (%d)\n", fail);

	return fail ? 1 : 0;
}
