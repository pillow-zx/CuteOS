#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

#include <utest.h>

UT_CASE(mm_brk_growth_public_musl_xfail, 1500)
{
	void *initial = sbrk(0);
	void *grown;

	UT_ASSERT(initial != (void *)-1);
	UT_XFAIL("musl intentionally exposes only sbrk(0), not brk growth");
	grown = sbrk(4096);
	UT_EXPECT_NE(grown, (void *)-1);
	UT_EXPECT((uintptr_t)sbrk(0) >= (uintptr_t)initial + 4096);
}

UT_CASE(mm_anonymous_protection_unmap_and_mremap, 5000)
{
	const size_t page_size = (size_t)sysconf(_SC_PAGESIZE);
	unsigned char *mapping;
	unsigned char *resized;
	pid_t child;

	UT_ASSERT(page_size > 0);
	mapping = mmap(NULL, page_size * 3, PROT_READ | PROT_WRITE,
		       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	UT_ASSERT(mapping != MAP_FAILED);
	UT_EXPECT_EQ(mapping[0], 0);
	mapping[0] = 1;
	mapping[page_size * 2] = 3;
	UT_ASSERT_EQ(mprotect(mapping, page_size, PROT_READ), 0);
	child = UT_FORK();
	if (child == 0) {
		mapping[0] = 2;
		_exit(127);
	}
	UT_EXPECT_SIGNAL(child, SIGSEGV);
	UT_ASSERT_EQ(mprotect(mapping, page_size, PROT_READ | PROT_WRITE), 0);
	UT_ASSERT_EQ(munmap(mapping + page_size, page_size), 0);
	child = UT_FORK();
	if (child == 0) {
		volatile unsigned char value = mapping[page_size];

		(void)value;
		_exit(127);
	}
	UT_EXPECT_SIGNAL(child, SIGSEGV);
	UT_EXPECT_EQ(mapping[0], 1);
	UT_EXPECT_EQ(mapping[page_size * 2], 3);
	UT_ASSERT_EQ(munmap(mapping, page_size), 0);
	UT_ASSERT_EQ(munmap(mapping + page_size * 2, page_size), 0);
	mapping = mmap(NULL, page_size, PROT_READ | PROT_WRITE,
		       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	UT_ASSERT(mapping != MAP_FAILED);
	mapping[0] = 42;
	resized = mremap(mapping, page_size, page_size * 2, MREMAP_MAYMOVE);
	UT_ASSERT(resized != MAP_FAILED);
	UT_EXPECT_EQ(resized[0], 42);
	UT_ASSERT_EQ(munmap(resized, page_size * 2), 0);
}

UT_CASE(mm_file_mapping_msync_mincore_and_madvise, 5000)
{
	const size_t page_size = (size_t)sysconf(_SC_PAGESIZE);
	unsigned char resident;
	char *path = ut_path("mapped");
	unsigned char *shared;
	unsigned char *anonymous;
	char check[8] = {};
	int fd;

	UT_ASSERT(page_size > 0);
	UT_ASSERT(path != NULL);
	fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0600);
	free(path);
	UT_ASSERT(fd >= 0);
	UT_ASSERT_EQ(ftruncate(fd, (off_t)page_size), 0);
	shared = mmap(NULL, page_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	UT_ASSERT(shared != MAP_FAILED);
	memcpy(shared, "mapped", 6);
	UT_ASSERT_EQ(msync(shared, page_size, MS_SYNC), 0);
	UT_ASSERT_EQ(munmap(shared, page_size), 0);
	UT_ASSERT_EQ(pread(fd, check, 6, 0), 6);
	UT_EXPECT_MEMEQ(check, "mapped", 6);
	UT_ASSERT_EQ(close(fd), 0);
	anonymous = mmap(NULL, page_size, PROT_READ | PROT_WRITE,
			 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	UT_ASSERT(anonymous != MAP_FAILED);
	anonymous[0] = 91;
	resident = 0;
	UT_ASSERT_EQ(mincore(anonymous, page_size, &resident), 0);
	UT_EXPECT(resident & 1);
	UT_ASSERT_EQ(madvise(anonymous, page_size, MADV_DONTNEED), 0);
	UT_EXPECT_EQ(anonymous[0], 0);
	UT_ASSERT_EQ(munmap(anonymous, page_size), 0);
}
