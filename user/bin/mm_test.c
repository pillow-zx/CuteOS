/*
 * user/bin/mm_test.c - memory-management user ABI tests
 */

#include <ulib.h>
#include <uapi/fcntl.h>
#include <uapi/mman.h>
#include <uapi/signal.h>

#define PAGE_SIZE	      4096UL
#define USER_STACK_GUARD_BASE 0x7FFFE000UL
#define USER_STACK_BASE	      0x7FFFF000UL

#define MMAP_TEST_FILE "/mmap_file_test"

STATIC_ASSERT(EOPNOTSUPP == 95, "EOPNOTSUPP ABI value mismatch");

static char mmap_file_data[PAGE_SIZE];
static char mmap_file_readback[PAGE_SIZE];
static char mmap_file_big_data[3 * PAGE_SIZE];

static int write_full(int fd, const char *buf, size_t len)
{
	size_t done = 0;

	while (done < len) {
		long ret = write(fd, buf + done, len - done);

		if (ret <= 0)
			return 1;
		done += (size_t)ret;
	}
	return 0;
}

static int read_full(int fd, char *buf, size_t len)
{
	size_t done = 0;

	while (done < len) {
		long ret = read(fd, buf + done, len - done);

		if (ret <= 0)
			return 1;
		done += (size_t)ret;
	}
	return 0;
}

static int create_mmap_test_file(const char *data, size_t len)
{
	int fd;

	unlinkat(AT_FDCWD, MMAP_TEST_FILE, 0);
	fd = openat(AT_FDCWD, MMAP_TEST_FILE, O_CREAT | O_RDWR | O_TRUNC, 0644);
	if (fd < 0) {
		printf("FAIL: open mmap test file: %d\n", fd);
		return -1;
	}
	if (write_full(fd, data, len)) {
		printf("FAIL: write mmap test file\n");
		close(fd);
		return -1;
	}
	close(fd);
	return 0;
}

static int test_brk_growth(void)
{
	long old_brk = brk(0);
	long new_brk;
	char *p;

	if (old_brk <= 0) {
		printf("FAIL: brk(0) returned %ld\n", old_brk);
		return 1;
	}

	new_brk = brk(old_brk + (long)PAGE_SIZE);
	if (new_brk != old_brk + (long)PAGE_SIZE) {
		printf("FAIL: brk grow: expected %ld got %ld\n",
		       old_brk + (long)PAGE_SIZE, new_brk);
		return 1;
	}

	p = (char *)old_brk;
	for (size_t i = 0; i < PAGE_SIZE; i++) {
		if (p[i] != 0) {
			printf("FAIL: brk page not zero at offset %zu\n", i);
			brk(old_brk);
			return 1;
		}
	}

	for (size_t i = 0; i < PAGE_SIZE; i++)
		p[i] = (char)(i & 0xff);
	for (size_t i = 0; i < PAGE_SIZE; i++) {
		if (p[i] != (char)(i & 0xff)) {
			printf("FAIL: brk page data mismatch at %zu\n", i);
			brk(old_brk);
			return 1;
		}
	}

	brk(old_brk);
	return 0;
}

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

	for (size_t i = 0; i < len; i++) {
		if (m[i] != 0) {
			printf("FAIL: mmap not zero at %zu\n", i);
			munmap(m, len);
			return 1;
		}
	}

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

	m[0] = 0xAA;
	m[len - 1] = 0xBB;

	long rc = munmap(m + PAGE_SIZE, 2 * PAGE_SIZE);

	if (rc != 0) {
		printf("FAIL: munmap middle: %ld\n", rc);
		munmap(m, len);
		return 1;
	}

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

	munmap(m, PAGE_SIZE);
	munmap(m + 3 * PAGE_SIZE, PAGE_SIZE);
	return 0;
}

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

	m[0] = 0xab;
	m[PAGE_SIZE] = 0xcd;

	ret = mprotect(m, PAGE_SIZE, PROT_READ);
	if (ret != 0) {
		printf("FAIL: mprotect PROT_READ: %ld\n", ret);
		munmap(m, 2 * PAGE_SIZE);
		return 1;
	}

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

	ret = mprotect(m, PAGE_SIZE, PROT_READ | PROT_WRITE);
	if (ret != 0) {
		printf("FAIL: mprotect restore RW: %ld\n", ret);
		munmap(m, 2 * PAGE_SIZE);
		return 1;
	}

	m[0] = 0x42;
	if (m[0] != 0x42) {
		printf("FAIL: write after restore failed\n");
		munmap(m, 2 * PAGE_SIZE);
		return 1;
	}

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

	m[0] = 1;
	m[PAGE_SIZE] = 2;
	m[2 * PAGE_SIZE] = 3;

	ret = mprotect(m + PAGE_SIZE, PAGE_SIZE, PROT_READ);
	if (ret != 0) {
		printf("FAIL: mprotect middle: %ld\n", ret);
		munmap(m, 3 * PAGE_SIZE);
		return 1;
	}

	m[0] = 0x11;
	m[2 * PAGE_SIZE] = 0x33;
	if (m[0] != 0x11 || m[2 * PAGE_SIZE] != 0x33) {
		printf("FAIL: adjacent pages not writable\n");
		munmap(m, 3 * PAGE_SIZE);
		return 1;
	}

	ret = mprotect(m + PAGE_SIZE, PAGE_SIZE, PROT_READ | PROT_WRITE);
	if (ret != 0) {
		printf("FAIL: mprotect restore middle: %ld\n", ret);
		munmap(m, 3 * PAGE_SIZE);
		return 1;
	}

	munmap(m, 3 * PAGE_SIZE);
	return 0;
}

static int test_mprotect_cross_vma(void)
{
	char *first;
	char *second;
	void *base = (void *)0x45000000UL;
	long ret;

	first = mmap(base, PAGE_SIZE, PROT_READ | PROT_WRITE,
		     MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
	if ((long)first < 0) {
		printf("FAIL: first cross-VMA mmap: %ld\n", (long)first);
		return 1;
	}

	second = mmap(first + PAGE_SIZE, PAGE_SIZE, PROT_READ,
		      MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
	if ((long)second < 0 || second != first + PAGE_SIZE) {
		printf("FAIL: second cross-VMA mmap: %ld\n", (long)second);
		if ((long)second >= 0)
			munmap(second, PAGE_SIZE);
		munmap(first, PAGE_SIZE);
		return 1;
	}

	ret = mprotect(first, 2 * PAGE_SIZE, PROT_READ | PROT_WRITE);
	if (ret != 0) {
		printf("FAIL: cross-VMA mprotect RW: %ld\n", ret);
		munmap(first, 2 * PAGE_SIZE);
		return 1;
	}

	first[0] = 0x11;
	second[0] = 0x22;
	if (first[0] != 0x11 || second[0] != 0x22) {
		printf("FAIL: cross-VMA write after mprotect\n");
		munmap(first, 2 * PAGE_SIZE);
		return 1;
	}

	ret = mprotect(first, 2 * PAGE_SIZE, PROT_READ);
	if (ret != 0) {
		printf("FAIL: cross-VMA mprotect R: %ld\n", ret);
		munmap(first, 2 * PAGE_SIZE);
		return 1;
	}

	ret = mprotect(first, 2 * PAGE_SIZE, PROT_READ | PROT_WRITE);
	if (ret != 0) {
		printf("FAIL: cross-VMA restore RW: %ld\n", ret);
		munmap(first, 2 * PAGE_SIZE);
		return 1;
	}

	munmap(first, 2 * PAGE_SIZE);
	return 0;
}

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
	if (ret != -22) {
		printf("FAIL: unaligned: expected -22 got %ld\n", ret);
		munmap(m, PAGE_SIZE);
		return 1;
	}

	munmap(m, PAGE_SIZE);
	return 0;
}

static int test_unmapped(void)
{
	long ret = mprotect((void *)0x100000, PAGE_SIZE, PROT_READ);

	if (ret != -12) {
		printf("FAIL: unmapped: expected -12 got %ld\n", ret);
		return 1;
	}
	return 0;
}

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
	if (ret != -22) {
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

static int test_madvise_private_file_dontneed(void)
{
	char *m;
	unsigned char vec[1];
	int fd;
	long ret;

	for (size_t i = 0; i < PAGE_SIZE; i++)
		mmap_file_data[i] = (char)(0x51 + (i % 31));

	if (create_mmap_test_file(mmap_file_data, sizeof(mmap_file_data)) < 0)
		return 1;

	fd = openat(AT_FDCWD, MMAP_TEST_FILE, O_RDONLY, 0);
	if (fd < 0) {
		printf("FAIL: open private DONTNEED file: %d\n", fd);
		return 1;
	}

	m = mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
	close(fd);
	if ((long)m < 0) {
		printf("FAIL: private DONTNEED mmap: %ld\n", (long)m);
		return 1;
	}

	if (m[3] != mmap_file_data[3]) {
		printf("FAIL: private DONTNEED initial read\n");
		munmap(m, PAGE_SIZE);
		return 1;
	}
	m[3] = 0x5a;

	vec[0] = 0;
	ret = mincore(m, PAGE_SIZE, vec);
	if (ret != 0 || !(vec[0] & 1)) {
		printf("FAIL: private DONTNEED resident ret=%ld vec=%u\n", ret,
		       vec[0]);
		munmap(m, PAGE_SIZE);
		return 1;
	}

	ret = madvise(m, PAGE_SIZE, MADV_DONTNEED);
	if (ret != 0) {
		printf("FAIL: private file MADV_DONTNEED: %ld\n", ret);
		munmap(m, PAGE_SIZE);
		return 1;
	}

	vec[0] = 1;
	ret = mincore(m, PAGE_SIZE, vec);
	if (ret != 0 || (vec[0] & 1)) {
		printf("FAIL: private DONTNEED dropped ret=%ld vec=%u\n", ret,
		       vec[0]);
		munmap(m, PAGE_SIZE);
		return 1;
	}

	if (m[3] != mmap_file_data[3]) {
		printf("FAIL: private DONTNEED did not reload file data\n");
		munmap(m, PAGE_SIZE);
		return 1;
	}

	munmap(m, PAGE_SIZE);
	return 0;
}

static int test_madvise_shared_file_dontneed(void)
{
	char *m;
	int fd;
	long ret;

	for (size_t i = 0; i < PAGE_SIZE; i++)
		mmap_file_data[i] = (char)(0x61 + (i % 29));

	if (create_mmap_test_file(mmap_file_data, sizeof(mmap_file_data)) < 0)
		return 1;

	fd = openat(AT_FDCWD, MMAP_TEST_FILE, O_RDWR, 0);
	if (fd < 0) {
		printf("FAIL: open shared DONTNEED file: %d\n", fd);
		return 1;
	}

	m = mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	close(fd);
	if ((long)m < 0) {
		printf("FAIL: shared DONTNEED mmap: %ld\n", (long)m);
		return 1;
	}

	m[5] = 0x6d;
	ret = madvise(m, PAGE_SIZE, MADV_DONTNEED);
	if (ret != 0) {
		printf("FAIL: shared file MADV_DONTNEED: %ld\n", ret);
		munmap(m, PAGE_SIZE);
		return 1;
	}

	if (m[5] != 0x6d) {
		printf("FAIL: shared DONTNEED lost dirty cache data\n");
		munmap(m, PAGE_SIZE);
		return 1;
	}

	ret = msync(m, PAGE_SIZE, MS_SYNC);
	if (ret != 0) {
		printf("FAIL: shared DONTNEED msync: %ld\n", ret);
		munmap(m, PAGE_SIZE);
		return 1;
	}
	munmap(m, PAGE_SIZE);

	fd = openat(AT_FDCWD, MMAP_TEST_FILE, O_RDONLY, 0);
	if (fd < 0) {
		printf("FAIL: reopen shared DONTNEED file: %d\n", fd);
		return 1;
	}
	if (read_full(fd, mmap_file_readback, PAGE_SIZE)) {
		printf("FAIL: read shared DONTNEED file\n");
		close(fd);
		return 1;
	}
	close(fd);

	if (mmap_file_readback[5] != 0x6d) {
		printf("FAIL: shared DONTNEED data not persisted\n");
		return 1;
	}

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
	if (ret != -22) {
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
	if (ret != -22) {
		printf("FAIL: unknown advice: expected -22 got %ld\n", ret);
		munmap(m, PAGE_SIZE);
		return 1;
	}

	munmap(m, PAGE_SIZE);
	return 0;
}

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

	m[0] = 42;

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

static int test_mincore_file_backed(void)
{
	char *m;
	unsigned char vec[1];
	int fd;
	long ret;

	for (size_t i = 0; i < PAGE_SIZE; i++)
		mmap_file_data[i] = (char)(0x41 + (i % 17));

	if (create_mmap_test_file(mmap_file_data, sizeof(mmap_file_data)) < 0)
		return 1;

	fd = openat(AT_FDCWD, MMAP_TEST_FILE, O_RDONLY, 0);
	if (fd < 0) {
		printf("FAIL: open mincore file: %d\n", fd);
		return 1;
	}

	m = mmap(NULL, PAGE_SIZE, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);
	if ((long)m < 0) {
		printf("FAIL: mincore file mmap: %ld\n", (long)m);
		return 1;
	}

	vec[0] = 1;
	ret = mincore(m, PAGE_SIZE, vec);
	if (ret != 0 || (vec[0] & 1)) {
		printf("FAIL: mincore file before fault ret=%ld vec=%u\n", ret,
		       vec[0]);
		munmap(m, PAGE_SIZE);
		return 1;
	}

	if (m[0] != mmap_file_data[0]) {
		printf("FAIL: mincore file fault read mismatch\n");
		munmap(m, PAGE_SIZE);
		return 1;
	}

	vec[0] = 0;
	ret = mincore(m, PAGE_SIZE, vec);
	if (ret != 0 || !(vec[0] & 1)) {
		printf("FAIL: mincore file after fault ret=%ld vec=%u\n", ret,
		       vec[0]);
		munmap(m, PAGE_SIZE);
		return 1;
	}

	munmap(m, PAGE_SIZE);
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
	if (ret != -22) {
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

static int test_mremap_maymove_preserves_data(void)
{
	char *m;
	char *blocker;
	char *remapped;
	unsigned char vec[2] = {0};
	const size_t old_len = 2 * PAGE_SIZE;
	const size_t new_len = 3 * PAGE_SIZE;
	void *base = (void *)0x41000000UL;
	long ret;

	m = mmap(base, old_len, PROT_READ | PROT_WRITE,
		 MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
	if ((long)m < 0) {
		printf("FAIL: fixed mmap for maymove: %ld\n", (long)m);
		return 1;
	}

	blocker = mmap(m + old_len, PAGE_SIZE, PROT_READ | PROT_WRITE,
		       MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
	if ((long)blocker < 0) {
		printf("FAIL: blocker mmap for maymove: %ld\n", (long)blocker);
		munmap(m, old_len);
		return 1;
	}

	m[0] = 0x21;
	m[PAGE_SIZE] = 0x32;
	m[old_len - 1] = 0x43;

	remapped = mremap(m, old_len, new_len, MREMAP_MAYMOVE, NULL);
	if ((long)remapped < 0) {
		printf("FAIL: mremap maymove: %ld\n", (long)remapped);
		munmap(blocker, PAGE_SIZE);
		munmap(m, old_len);
		return 1;
	}
	if (remapped == m) {
		printf("FAIL: mremap maymove did not move blocked mapping\n");
		munmap(remapped, new_len);
		munmap(blocker, PAGE_SIZE);
		return 1;
	}
	if (remapped[0] != 0x21 || remapped[PAGE_SIZE] != 0x32 ||
	    remapped[old_len - 1] != 0x43) {
		printf("FAIL: mremap maymove lost old data\n");
		munmap(remapped, new_len);
		munmap(blocker, PAGE_SIZE);
		return 1;
	}
	if (remapped[old_len] != 0) {
		printf("FAIL: mremap maymove new page not zero-filled\n");
		munmap(remapped, new_len);
		munmap(blocker, PAGE_SIZE);
		return 1;
	}
	remapped[new_len - 1] = 0x54;
	if (remapped[new_len - 1] != 0x54) {
		printf("FAIL: mremap maymove new page not writable\n");
		munmap(remapped, new_len);
		munmap(blocker, PAGE_SIZE);
		return 1;
	}

	ret = mincore(m, old_len, vec);
	if (ret != -12) {
		printf("FAIL: mremap maymove old range mincore=%ld\n", ret);
		munmap(remapped, new_len);
		munmap(blocker, PAGE_SIZE);
		return 1;
	}

	munmap(remapped, new_len);
	munmap(blocker, PAGE_SIZE);
	return 0;
}

static int test_mremap_fixed_move_replaces_target(void)
{
	char *m;
	char *target;
	char *remapped;
	unsigned char vec[2] = {0};
	const size_t len = 2 * PAGE_SIZE;
	void *base = (void *)0x42000000UL;
	void *target_base = (void *)0x43000000UL;
	long ret;

	m = mmap(base, len, PROT_READ | PROT_WRITE,
		 MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
	if ((long)m < 0) {
		printf("FAIL: fixed source mmap: %ld\n", (long)m);
		return 1;
	}

	target = mmap(target_base, len, PROT_READ | PROT_WRITE,
		      MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
	if ((long)target < 0) {
		printf("FAIL: fixed target mmap: %ld\n", (long)target);
		munmap(m, len);
		return 1;
	}

	m[0] = 0x61;
	m[PAGE_SIZE] = 0x72;
	target[0] = 0x33;

	remapped =
		mremap(m, len, len, MREMAP_MAYMOVE | MREMAP_FIXED, target_base);
	if ((long)remapped < 0) {
		printf("FAIL: mremap fixed move: %ld\n", (long)remapped);
		munmap(target, len);
		munmap(m, len);
		return 1;
	}
	if (remapped != target || target[0] != 0x61 ||
	    target[PAGE_SIZE] != 0x72) {
		printf("FAIL: mremap fixed move data/target mismatch\n");
		munmap(target, len);
		return 1;
	}

	ret = mincore(m, len, vec);
	if (ret != -12) {
		printf("FAIL: mremap fixed old range mincore=%ld\n", ret);
		munmap(target, len);
		return 1;
	}

	munmap(target, len);
	return 0;
}

static int test_mremap_file_subrange_keeps_offset(void)
{
	char *m;
	char *blocker;
	char *remapped;
	const size_t map_len = 2 * PAGE_SIZE;
	const size_t old_len = PAGE_SIZE;
	const size_t new_len = 2 * PAGE_SIZE;
	void *base = (void *)0x44000000UL;
	int fd;

	for (size_t i = 0; i < PAGE_SIZE; i++) {
		mmap_file_big_data[i] = (char)0x11;
		mmap_file_big_data[PAGE_SIZE + i] = (char)0x22;
		mmap_file_big_data[2 * PAGE_SIZE + i] = (char)0x33;
	}
	if (create_mmap_test_file(mmap_file_big_data,
				  sizeof(mmap_file_big_data)) < 0)
		return 1;

	fd = openat(AT_FDCWD, MMAP_TEST_FILE, O_RDONLY, 0);
	if (fd < 0) {
		printf("FAIL: open file subrange mremap file: %d\n", fd);
		return 1;
	}

	m = mmap(base, map_len, PROT_READ, MAP_PRIVATE | MAP_FIXED, fd, 0);
	close(fd);
	if ((long)m < 0) {
		printf("FAIL: file subrange mmap: %ld\n", (long)m);
		return 1;
	}

	blocker = mmap(m + map_len, PAGE_SIZE, PROT_READ | PROT_WRITE,
		       MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
	if ((long)blocker < 0) {
		printf("FAIL: file subrange blocker mmap: %ld\n",
		       (long)blocker);
		munmap(m, map_len);
		return 1;
	}

	remapped =
		mremap(m + PAGE_SIZE, old_len, new_len, MREMAP_MAYMOVE, NULL);
	if ((long)remapped < 0) {
		printf("FAIL: file subrange mremap: %ld\n", (long)remapped);
		munmap(blocker, PAGE_SIZE);
		munmap(m, map_len);
		return 1;
	}
	if (remapped == m + PAGE_SIZE) {
		printf("FAIL: file subrange mremap did not move\n");
		munmap(remapped, new_len);
		munmap(blocker, PAGE_SIZE);
		munmap(m, PAGE_SIZE);
		return 1;
	}
	if (remapped[0] != 0x22 || remapped[PAGE_SIZE] != 0x33) {
		printf("FAIL: file subrange mremap offset mismatch\n");
		munmap(remapped, new_len);
		munmap(blocker, PAGE_SIZE);
		munmap(m, PAGE_SIZE);
		return 1;
	}
	if (m[0] != 0x11) {
		printf("FAIL: file subrange mremap damaged prefix\n");
		munmap(remapped, new_len);
		munmap(blocker, PAGE_SIZE);
		munmap(m, PAGE_SIZE);
		return 1;
	}

	munmap(remapped, new_len);
	munmap(blocker, PAGE_SIZE);
	munmap(m, PAGE_SIZE);
	return 0;
}

static int test_mremap_invalid_flags(void)
{
	char *m;
	void *target = (void *)0x47000000UL;
	long ret;

	m = mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE,
		 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if ((long)m < 0) {
		printf("FAIL: mmap for invalid mremap: %ld\n", (long)m);
		return 1;
	}

	ret = (long)mremap(m, PAGE_SIZE, 2 * PAGE_SIZE, MREMAP_FIXED, target);
	if (ret != -EINVAL) {
		printf("FAIL: MREMAP_FIXED without MAYMOVE got %ld\n", ret);
		munmap(m, PAGE_SIZE);
		return 1;
	}

	ret = (long)mremap(m, PAGE_SIZE, PAGE_SIZE,
			   MREMAP_MAYMOVE | MREMAP_FIXED,
			   (void *)((unsigned long)target + 1));
	if (ret != -EINVAL) {
		printf("FAIL: MREMAP_FIXED unaligned target got %ld\n", ret);
		munmap(m, PAGE_SIZE);
		return 1;
	}

	ret = (long)mremap(m, PAGE_SIZE, PAGE_SIZE, MREMAP_DONTUNMAP, NULL);
	if (ret != -EINVAL) {
		printf("FAIL: MREMAP_DONTUNMAP got %ld\n", ret);
		munmap(m, PAGE_SIZE);
		return 1;
	}

	ret = (long)mremap(m, PAGE_SIZE, PAGE_SIZE, 0x100, NULL);
	if (ret != -EINVAL) {
		printf("FAIL: unknown mremap flag got %ld\n", ret);
		munmap(m, PAGE_SIZE);
		return 1;
	}

	munmap(m, PAGE_SIZE);
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

static int test_mmap_shared_file_read(void)
{
	char *m;
	int fd;

	for (size_t i = 0; i < PAGE_SIZE; i++)
		mmap_file_data[i] = (char)(0x31 + (i % 23));

	if (create_mmap_test_file(mmap_file_data, sizeof(mmap_file_data)) < 0)
		return 1;

	fd = openat(AT_FDCWD, MMAP_TEST_FILE, O_RDWR, 0);
	if (fd < 0) {
		printf("FAIL: open shared mmap file: %d\n", fd);
		return 1;
	}

	m = mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	close(fd);
	if ((long)m < 0) {
		printf("FAIL: shared file mmap: %ld\n", (long)m);
		return 1;
	}

	for (size_t i = 0; i < PAGE_SIZE; i++) {
		if (m[i] != mmap_file_data[i]) {
			printf("FAIL: shared file mmap data mismatch at %zu\n",
			       i);
			munmap(m, PAGE_SIZE);
			return 1;
		}
	}

	munmap(m, PAGE_SIZE);
	return 0;
}

static int test_msync_shared_file_writeback(void)
{
	char *m;
	int fd;
	long ret;

	for (size_t i = 0; i < PAGE_SIZE; i++)
		mmap_file_data[i] = (char)(0x41 + (i % 17));

	if (create_mmap_test_file(mmap_file_data, sizeof(mmap_file_data)) < 0)
		return 1;

	fd = openat(AT_FDCWD, MMAP_TEST_FILE, O_RDWR, 0);
	if (fd < 0) {
		printf("FAIL: open shared writeback file: %d\n", fd);
		return 1;
	}

	m = mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	close(fd);
	if ((long)m < 0) {
		printf("FAIL: shared writeback mmap: %ld\n", (long)m);
		return 1;
	}

	m[7] = 0x55;
	m[PAGE_SIZE - 9] = 0x66;
	ret = msync(m, PAGE_SIZE, MS_SYNC);
	if (ret != 0) {
		printf("FAIL: shared writeback msync: %ld\n", ret);
		munmap(m, PAGE_SIZE);
		return 1;
	}
	munmap(m, PAGE_SIZE);

	fd = openat(AT_FDCWD, MMAP_TEST_FILE, O_RDONLY, 0);
	if (fd < 0) {
		printf("FAIL: reopen shared writeback file: %d\n", fd);
		return 1;
	}
	if (read_full(fd, mmap_file_readback, PAGE_SIZE)) {
		printf("FAIL: read shared writeback file\n");
		close(fd);
		return 1;
	}
	close(fd);

	if (mmap_file_readback[7] != 0x55 ||
	    mmap_file_readback[PAGE_SIZE - 9] != 0x66) {
		printf("FAIL: shared writeback data not persisted\n");
		return 1;
	}

	return 0;
}

static int test_mmap_private_file_no_writeback(void)
{
	char *m;
	int fd;
	long ret;

	for (size_t i = 0; i < PAGE_SIZE; i++)
		mmap_file_data[i] = (char)(0x21 + (i % 29));

	if (create_mmap_test_file(mmap_file_data, sizeof(mmap_file_data)) < 0)
		return 1;

	fd = openat(AT_FDCWD, MMAP_TEST_FILE, O_RDWR, 0);
	if (fd < 0) {
		printf("FAIL: open private mmap file: %d\n", fd);
		return 1;
	}

	m = mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
	close(fd);
	if ((long)m < 0) {
		printf("FAIL: private file mmap: %ld\n", (long)m);
		return 1;
	}
	if (m[11] != mmap_file_data[11]) {
		printf("FAIL: private mmap initial read mismatch\n");
		munmap(m, PAGE_SIZE);
		return 1;
	}

	m[11] = 0x12;
	m[PAGE_SIZE - 13] = 0x34;
	ret = msync(m, PAGE_SIZE, MS_SYNC);
	if (ret != 0) {
		printf("FAIL: private mmap msync: %ld\n", ret);
		munmap(m, PAGE_SIZE);
		return 1;
	}
	munmap(m, PAGE_SIZE);

	fd = openat(AT_FDCWD, MMAP_TEST_FILE, O_RDONLY, 0);
	if (fd < 0) {
		printf("FAIL: reopen private mmap file: %d\n", fd);
		return 1;
	}
	if (read_full(fd, mmap_file_readback, PAGE_SIZE)) {
		printf("FAIL: read private mmap file\n");
		close(fd);
		return 1;
	}
	close(fd);

	if (mmap_file_readback[11] != mmap_file_data[11] ||
	    mmap_file_readback[PAGE_SIZE - 13] !=
		    mmap_file_data[PAGE_SIZE - 13]) {
		printf("FAIL: private mmap wrote back to file\n");
		return 1;
	}

	return 0;
}

static int test_mmap_populate_file_resident(void)
{
	char *m;
	unsigned char vec[1];
	int fd;
	long ret;

	for (size_t i = 0; i < PAGE_SIZE; i++)
		mmap_file_data[i] = (char)(0x51 + (i % 19));

	if (create_mmap_test_file(mmap_file_data, sizeof(mmap_file_data)) < 0)
		return 1;

	fd = openat(AT_FDCWD, MMAP_TEST_FILE, O_RDONLY, 0);
	if (fd < 0) {
		printf("FAIL: open populate mmap file: %d\n", fd);
		return 1;
	}

	m = mmap(NULL, PAGE_SIZE, PROT_READ, MAP_PRIVATE | MAP_POPULATE, fd, 0);
	close(fd);
	if ((long)m < 0) {
		printf("FAIL: populate file mmap: %ld\n", (long)m);
		return 1;
	}

	vec[0] = 0;
	ret = mincore(m, PAGE_SIZE, vec);
	if (ret != 0 || !(vec[0] & 1)) {
		printf("FAIL: populate file not resident ret=%ld vec=%u\n", ret,
		       vec[0]);
		munmap(m, PAGE_SIZE);
		return 1;
	}

	if (m[17] != mmap_file_data[17]) {
		printf("FAIL: populate file data mismatch\n");
		munmap(m, PAGE_SIZE);
		return 1;
	}

	munmap(m, PAGE_SIZE);
	return 0;
}

static int test_mmap_fixed_replaces_mapping(void)
{
	char *anon;
	char *fixed;
	int fd;
	void *base = (void *)0x42000000UL;

	for (size_t i = 0; i < PAGE_SIZE; i++)
		mmap_file_data[i] = (char)(0x61 + (i % 19));

	if (create_mmap_test_file(mmap_file_data, sizeof(mmap_file_data)) < 0)
		return 1;

	anon = mmap(base, PAGE_SIZE, PROT_READ | PROT_WRITE,
		    MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
	if ((long)anon < 0) {
		printf("FAIL: fixed anonymous mmap: %ld\n", (long)anon);
		return 1;
	}
	anon[0] = 0x7a;

	fd = openat(AT_FDCWD, MMAP_TEST_FILE, O_RDWR, 0);
	if (fd < 0) {
		printf("FAIL: open fixed replacement file: %d\n", fd);
		munmap(anon, PAGE_SIZE);
		return 1;
	}

	fixed = mmap(base, PAGE_SIZE, PROT_READ | PROT_WRITE,
		     MAP_SHARED | MAP_FIXED, fd, 0);
	close(fd);
	if (fixed != anon) {
		printf("FAIL: MAP_FIXED replacement returned %ld\n",
		       (long)fixed);
		if ((long)fixed >= 0)
			munmap(fixed, PAGE_SIZE);
		else
			munmap(anon, PAGE_SIZE);
		return 1;
	}

	if (fixed[0] != mmap_file_data[0] ||
	    fixed[PAGE_SIZE - 1] != mmap_file_data[PAGE_SIZE - 1]) {
		printf("FAIL: MAP_FIXED replacement did not expose file "
		       "data\n");
		munmap(fixed, PAGE_SIZE);
		return 1;
	}

	munmap(fixed, PAGE_SIZE);
	return 0;
}

static int test_mmap_flag_policy(void)
{
	char *m;
	char *noreplace;
	char *hint;
	char *populate;
	char *validate;
	unsigned char vec[1];
	void *base = (void *)0x46000000UL;
	long ret;

	m = mmap(base, PAGE_SIZE, PROT_READ | PROT_WRITE,
		 MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
	if ((long)m < 0) {
		printf("FAIL: mmap flag base: %ld\n", (long)m);
		return 1;
	}

	ret = (long)mmap(base, PAGE_SIZE, PROT_READ | PROT_WRITE,
			 MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1,
			 0);
	if (ret != -EEXIST) {
		printf("FAIL: MAP_FIXED_NOREPLACE overlap got %ld\n", ret);
		if (ret >= 0)
			munmap((void *)ret, PAGE_SIZE);
		munmap(m, PAGE_SIZE);
		return 1;
	}

	noreplace =
		mmap(m + PAGE_SIZE, PAGE_SIZE, PROT_READ | PROT_WRITE,
		     MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
	if ((long)noreplace < 0 || noreplace != m + PAGE_SIZE) {
		printf("FAIL: MAP_FIXED_NOREPLACE free got %ld\n",
		       (long)noreplace);
		if ((long)noreplace >= 0)
			munmap(noreplace, PAGE_SIZE);
		munmap(m, PAGE_SIZE);
		return 1;
	}

	hint = mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE,
		    MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK | MAP_NORESERVE, -1,
		    0);
	if ((long)hint < 0) {
		printf("FAIL: mmap hint flags: %ld\n", (long)hint);
		munmap(m, 2 * PAGE_SIZE);
		return 1;
	}

	validate = mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE,
			MAP_SHARED_VALIDATE | MAP_ANONYMOUS, -1, 0);
	if ((long)validate < 0) {
		printf("FAIL: MAP_SHARED_VALIDATE got %ld\n", (long)validate);
		munmap(hint, PAGE_SIZE);
		munmap(m, 2 * PAGE_SIZE);
		return 1;
	}
	validate[0] = 0x5a;
	if (validate[0] != 0x5a) {
		printf("FAIL: MAP_SHARED_VALIDATE write/read\n");
		munmap(validate, PAGE_SIZE);
		munmap(hint, PAGE_SIZE);
		munmap(m, 2 * PAGE_SIZE);
		return 1;
	}

	ret = (long)mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE,
			 MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB, -1, 0);
	if (ret != -EINVAL) {
		printf("FAIL: MAP_HUGETLB expected -EINVAL got %ld\n", ret);
		if (ret >= 0)
			munmap((void *)ret, PAGE_SIZE);
		munmap(validate, PAGE_SIZE);
		munmap(hint, PAGE_SIZE);
		munmap(m, 2 * PAGE_SIZE);
		return 1;
	}

	ret = (long)mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE,
			 MAP_PRIVATE | MAP_ANONYMOUS | MAP_LOCKED, -1, 0);
	if (ret != -EINVAL) {
		printf("FAIL: MAP_LOCKED expected -EINVAL got %ld\n", ret);
		if (ret >= 0)
			munmap((void *)ret, PAGE_SIZE);
		munmap(validate, PAGE_SIZE);
		munmap(hint, PAGE_SIZE);
		munmap(m, 2 * PAGE_SIZE);
		return 1;
	}

	ret = (long)mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE,
			 MAP_PRIVATE | MAP_ANONYMOUS | MAP_NONBLOCK, -1, 0);
	if (ret != -EINVAL) {
		printf("FAIL: MAP_NONBLOCK expected -EINVAL got %ld\n", ret);
		if (ret >= 0)
			munmap((void *)ret, PAGE_SIZE);
		munmap(validate, PAGE_SIZE);
		munmap(hint, PAGE_SIZE);
		munmap(m, 2 * PAGE_SIZE);
		return 1;
	}

	ret = (long)mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE,
			 MAP_SHARED_VALIDATE | MAP_ANONYMOUS | MAP_SYNC, -1, 0);
	if (ret != -EOPNOTSUPP) {
		printf("FAIL: MAP_SHARED_VALIDATE unsupported expected %d got "
		       "%ld\n",
		       -EOPNOTSUPP, ret);
		if (ret >= 0)
			munmap((void *)ret, PAGE_SIZE);
		munmap(validate, PAGE_SIZE);
		munmap(hint, PAGE_SIZE);
		munmap(m, 2 * PAGE_SIZE);
		return 1;
	}

	populate = mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE,
			MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE, -1, 0);
	if ((long)populate < 0) {
		printf("FAIL: MAP_POPULATE anonymous got %ld\n",
		       (long)populate);
		munmap(validate, PAGE_SIZE);
		munmap(hint, PAGE_SIZE);
		munmap(m, 2 * PAGE_SIZE);
		return 1;
	}

	vec[0] = 0;
	ret = mincore(populate, PAGE_SIZE, vec);
	if (ret != 0 || !(vec[0] & 1)) {
		printf("FAIL: MAP_POPULATE anonymous not resident ret=%ld "
		       "vec=%u\n",
		       ret, vec[0]);
		munmap(populate, PAGE_SIZE);
		munmap(validate, PAGE_SIZE);
		munmap(hint, PAGE_SIZE);
		munmap(m, 2 * PAGE_SIZE);
		return 1;
	}

	munmap(populate, PAGE_SIZE);
	munmap(validate, PAGE_SIZE);
	munmap(hint, PAGE_SIZE);
	munmap(m, 2 * PAGE_SIZE);
	return 0;
}

static int test_mmap_rejects_stack_guard(void)
{
	void *guard = (void *)USER_STACK_GUARD_BASE;
	void *ret;

	ret = mmap(guard, PAGE_SIZE, PROT_READ | PROT_WRITE,
		   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
	if ((long)ret != -EINVAL) {
		printf("FAIL: stack guard mmap expected %d got %ld\n", -EINVAL,
		       (long)ret);
		if ((long)ret >= 0)
			munmap(ret, PAGE_SIZE);
		return 1;
	}

	return 0;
}

static int test_mlock_munlock_compat(void)
{
	char *m;
	unsigned char vec[1] = {0};
	long ret;
	int failed = 0;

	m = mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE,
		 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if ((long)m < 0) {
		printf("FAIL: mmap for mlock: %ld\n", (long)m);
		return 1;
	}

	ret = mlock(m + 1, 1);
	if (ret != 0) {
		printf("FAIL: mlock unaligned mapped expected 0 got %ld\n",
		       ret);
		munmap(m, PAGE_SIZE);
		return 1;
	}

	ret = mincore(m, PAGE_SIZE, vec);
	if (ret != 0 || vec[0] != 1) {
		printf("FAIL: mlock did not make page resident ret=%ld "
		       "vec=%u\n",
		       ret, vec[0]);
		failed++;
	}

	ret = munlock(m + 1, 1);
	if (ret != 0) {
		printf("FAIL: munlock unaligned mapped expected 0 got %ld\n",
		       ret);
		failed++;
	}

	ret = mlock(NULL, 0);
	if (ret != 0) {
		printf("FAIL: mlock zero length expected 0 got %ld\n", ret);
		failed++;
	}

	ret = munlock(NULL, 0);
	if (ret != 0) {
		printf("FAIL: munlock zero length expected 0 got %ld\n", ret);
		failed++;
	}

	ret = mlock((void *)0x100000, PAGE_SIZE);
	if (ret != -ENOMEM) {
		printf("FAIL: mlock unmapped expected %d got %ld\n", -ENOMEM,
		       ret);
		failed++;
	}

	ret = munlock((void *)0x100000, PAGE_SIZE);
	if (ret != -ENOMEM) {
		printf("FAIL: munlock unmapped expected %d got %ld\n", -ENOMEM,
		       ret);
		failed++;
	}

	munmap(m, PAGE_SIZE);
	return failed;
}

static int test_stack_underflow_sigsegv(void)
{
	volatile char *underflow = (volatile char *)(USER_STACK_BASE - 1);
	long pid;
	int status;

	pid = fork();
	if (pid == 0) {
		*underflow = 0x5a;
		exit(7);
	}
	if (pid < 0) {
		printf("FAIL: fork stack underflow: %ld\n", pid);
		return 1;
	}
	if (wait4(pid, &status, 0, NULL) != pid) {
		printf("FAIL: wait4 stack underflow child\n");
		return 1;
	}
	if (status != (SIGNAL_EXIT_CODE(SIGSEGV) << 8)) {
		printf("FAIL: stack underflow status=0x%x\n", status);
		return 1;
	}

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
	report_group("mprotect across VMAs", test_mprotect_cross_vma(),
		     &failed);
	report_group("mprotect unaligned", test_unaligned(), &failed);
	report_group("mprotect unmapped", test_unmapped(), &failed);
	report_group("madvise valid advice", test_madvise_valid(), &failed);
	report_group("madvise private file DONTNEED",
		     test_madvise_private_file_dontneed(), &failed);
	report_group("madvise shared file DONTNEED",
		     test_madvise_shared_file_dontneed(), &failed);
	report_group("madvise unaligned", test_madvise_unaligned(), &failed);
	report_group("madvise unknown advice", test_madvise_unknown_advice(),
		     &failed);
	report_group("mincore basic", test_mincore_basic(), &failed);
	report_group("mincore file-backed", test_mincore_file_backed(),
		     &failed);
	report_group("mincore unaligned", test_mincore_unaligned(), &failed);
	report_group("mincore zero length", test_mincore_zero_len(), &failed);
	report_group("mincore PROT_NONE resident",
		     test_mincore_prot_none_resident(), &failed);
	report_group("mremap shrink anonymous", test_mremap_shrink_anon(),
		     &failed);
	report_group("mremap grow anonymous fixed",
		     test_mremap_grow_anon_fixed(), &failed);
	report_group("mremap maymove preserves data",
		     test_mremap_maymove_preserves_data(), &failed);
	report_group("mremap fixed move replaces target",
		     test_mremap_fixed_move_replaces_target(), &failed);
	report_group("mremap file subrange keeps offset",
		     test_mremap_file_subrange_keeps_offset(), &failed);
	report_group("mremap invalid flags", test_mremap_invalid_flags(),
		     &failed);
	report_group("msync anonymous", test_msync_anon(), &failed);
	report_group("mmap shared file read", test_mmap_shared_file_read(),
		     &failed);
	report_group("msync shared file writeback",
		     test_msync_shared_file_writeback(), &failed);
	report_group("mmap private file no writeback",
		     test_mmap_private_file_no_writeback(), &failed);
	report_group("mmap populate file resident",
		     test_mmap_populate_file_resident(), &failed);
	report_group("mmap fixed replaces mapping",
		     test_mmap_fixed_replaces_mapping(), &failed);
	report_group("mmap flag policy", test_mmap_flag_policy(), &failed);
	report_group("mmap rejects stack guard",
		     test_mmap_rejects_stack_guard(), &failed);
	report_group("mlock munlock compatibility", test_mlock_munlock_compat(),
		     &failed);
	report_group("stack underflow SIGSEGV", test_stack_underflow_sigsegv(),
		     &failed);

	if (failed)
		printf("mm_test: %d test group(s) FAILED\n", failed);
	else
		printf("mm_test: all tests PASSED\n");

	return failed ? 1 : 0;
}
