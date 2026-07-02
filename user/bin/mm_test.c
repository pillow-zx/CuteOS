/*
 * user/bin/mm_test.c - memory-management user ABI tests
 */

#include <ulib.h>
#include <uapi/mman.h>
#include <uapi/signal.h>

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
	m[0] = 1;
	m[PAGE_SIZE] = 2;
	m[2 * PAGE_SIZE] = 3;

	/* Protect only the middle page read-only. */
	ret = mprotect(m + PAGE_SIZE, PAGE_SIZE, PROT_READ);
	if (ret != 0) {
		printf("FAIL: mprotect middle: %ld\n", ret);
		munmap(m, 3 * PAGE_SIZE);
		return 1;
	}

	/* First and last pages still writable. */
	m[0] = 0x11;
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
		printf("FAIL: PROT_NONE restore ret=%ld value=0x%x\n", ret,
		       (unsigned char)m[0]);
		munmap(m, PAGE_SIZE);
		return 1;
	}

	munmap(m, PAGE_SIZE);
	return 0;
}

static int test_mremap_shrink_anon(void)
{
	char *m;
	void *remapped;
	const size_t old_len = 2 * PAGE_SIZE;
	const size_t new_len = PAGE_SIZE;

	m = mmap(NULL, old_len, PROT_READ | PROT_WRITE,
		 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if ((long)m < 0) {
		printf("FAIL: mmap: %ld\n", (long)m);
		return 1;
	}

	m[0] = 0x31;
	m[PAGE_SIZE] = 0x42;
	remapped = mremap(m, old_len, new_len, 0, NULL);
	if ((long)remapped < 0) {
		printf("FAIL: mremap shrink: %ld\n", (long)remapped);
		munmap(m, old_len);
		return 1;
	}
	if (remapped != m || m[0] != 0x31) {
		printf("FAIL: mremap shrink lost mapping/data\n");
		munmap(remapped, new_len);
		return 1;
	}

	munmap(remapped, new_len);
	return 0;
}

static int test_mremap_grow_anon_fixed(void)
{
	char *m;
	void *remapped;
	const size_t old_len = PAGE_SIZE;
	const size_t new_len = 2 * PAGE_SIZE;
	void *base = (void *)0x40000000UL;

	m = mmap(base, old_len, PROT_READ | PROT_WRITE,
		 MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
	if ((long)m < 0) {
		printf("FAIL: fixed mmap: %ld\n", (long)m);
		return 1;
	}

	m[0] = 0x52;
	remapped = mremap(m, old_len, new_len, 0, NULL);
	if ((long)remapped < 0) {
		printf("FAIL: mremap grow: %ld\n", (long)remapped);
		munmap(m, old_len);
		return 1;
	}
	if (remapped != m || m[0] != 0x52) {
		printf("FAIL: mremap grow lost old data\n");
		munmap(remapped, new_len);
		return 1;
	}

	m[PAGE_SIZE] = 0x63;
	if (m[PAGE_SIZE] != 0x63) {
		printf("FAIL: mremap grow new page not writable\n");
		munmap(remapped, new_len);
		return 1;
	}

	munmap(remapped, new_len);
	return 0;
}

static int test_msync_anon(void)
{
	char *m;
	long ret;

	m = mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE,
		 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if ((long)m < 0) {
		printf("FAIL: mmap: %ld\n", (long)m);
		return 1;
	}

	m[0] = 0x27;
	ret = msync(m, PAGE_SIZE, MS_SYNC);
	if (ret != 0) {
		printf("FAIL: msync anon: %ld\n", ret);
		munmap(m, PAGE_SIZE);
		return 1;
	}
	ret = msync(m + 1, PAGE_SIZE - 1, MS_SYNC);
	if (ret != -22) {
		printf("FAIL: msync unaligned expected -22 got %ld\n", ret);
		munmap(m, PAGE_SIZE);
		return 1;
	}
	ret = msync(m, PAGE_SIZE, MS_SYNC | MS_ASYNC);
	if (ret != -22) {
		printf("FAIL: msync conflicting flags expected -22 got %ld\n",
		       ret);
		munmap(m, PAGE_SIZE);
		return 1;
	}
	ret = msync(m, PAGE_SIZE, 0x1000);
	if (ret != -22) {
		printf("FAIL: msync unknown flags expected -22 got %ld\n", ret);
		munmap(m, PAGE_SIZE);
		return 1;
	}

	munmap(m, PAGE_SIZE);
	return 0;
}

static void report_group(const char *name, int ret, int *failed)
{
	printf("mm_test: %s ... ", name);
	if (ret)
		(*failed)++;
	else
		printf("PASS\n");
}

int main(void)
{
	int failed = 0;

	report_group("brk growth", test_brk_growth(), &failed);
	report_group("mmap anonymous", test_mmap_anon(), &failed);
	report_group("munmap middle split", test_munmap_split(), &failed);
	report_group("multiple independent mappings", test_mmap_independent(),
		     &failed);
	report_group("mprotect read-only and restore", test_ro_rw(), &failed);
	report_group("mprotect partial range", test_partial_range(), &failed);
	report_group("mprotect unaligned", test_unaligned(), &failed);
	report_group("mprotect unmapped", test_unmapped(), &failed);
	report_group("madvise valid advice", test_madvise_valid(), &failed);
	report_group("madvise unaligned", test_madvise_unaligned(), &failed);
	report_group("madvise unknown advice", test_madvise_unknown_advice(),
		     &failed);
	report_group("mincore basic", test_mincore_basic(), &failed);
	report_group("mincore unaligned", test_mincore_unaligned(), &failed);
	report_group("mincore zero length", test_mincore_zero_len(), &failed);
	report_group("mincore PROT_NONE resident",
		     test_mincore_prot_none_resident(), &failed);
	report_group("mremap shrink anonymous", test_mremap_shrink_anon(),
		     &failed);
	report_group("mremap grow anonymous fixed",
		     test_mremap_grow_anon_fixed(), &failed);
	report_group("msync anonymous", test_msync_anon(), &failed);

	if (failed)
		printf("mm_test: %d test group(s) FAILED\n", failed);
	else
		printf("mm_test: all tests PASSED\n");

	return failed ? 1 : 0;
}
