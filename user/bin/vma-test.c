#include <ulib.h>

#define PAGE_SIZE 4096UL
#define NR_VMA	  16
#define ENOMEM	  12

#define BASE_MERGE 0x02000000UL
#define BASE_SPLIT 0x03000000UL
#define BASE_FULL  0x04000000UL
#define VMA_GAP	   0x00100000UL
#define MAX_FILLERS 64

static int expect_mmap(unsigned long addr, size_t length, int prot)
{
	void *ret = mmap((void *)addr, length, prot,
			 MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);

	if ((unsigned long)ret != addr) {
		printf("vma-test: mmap %p len=%lu failed: %ld\n", (void *)addr,
		       (unsigned long)length, (long)ret);
		return -1;
	}

	return 0;
}

static int test_adjacent_merge(void)
{
	unsigned long len = (NR_VMA + 4) * PAGE_SIZE;

	for (int i = 0; i < NR_VMA + 4; i++) {
		if (expect_mmap(BASE_MERGE + (unsigned long)i * PAGE_SIZE,
				PAGE_SIZE, PROT_READ | PROT_WRITE) < 0)
			return -1;
	}

	volatile char *mem = (volatile char *)BASE_MERGE;
	mem[0] = 0x11;
	mem[len - 1] = 0x22;
	if (mem[0] != 0x11 || mem[len - 1] != 0x22) {
		printf("vma-test: adjacent merged pages lost data\n");
		return -1;
	}

	if (munmap((void *)BASE_MERGE, len) < 0) {
		printf("vma-test: cleanup adjacent merge failed\n");
		return -1;
	}

	return 0;
}

static int test_split_remap(void)
{
	volatile char *mem = (volatile char *)BASE_SPLIT;

	if (expect_mmap(BASE_SPLIT, 3 * PAGE_SIZE,
			PROT_READ | PROT_WRITE) < 0)
		return -1;

	mem[0] = 0x31;
	mem[2 * PAGE_SIZE] = 0x33;

	if (munmap((void *)(BASE_SPLIT + PAGE_SIZE), PAGE_SIZE) != 0) {
		printf("vma-test: middle munmap failed\n");
		return -1;
	}

	if (expect_mmap(BASE_SPLIT + PAGE_SIZE, PAGE_SIZE,
			PROT_READ | PROT_WRITE) < 0)
		return -1;

	mem[PAGE_SIZE] = 0x32;
	if (mem[0] != 0x31 || mem[PAGE_SIZE] != 0x32 ||
	    mem[2 * PAGE_SIZE] != 0x33) {
		printf("vma-test: split/remap data check failed\n");
		return -1;
	}

	if (munmap((void *)BASE_SPLIT, 3 * PAGE_SIZE) < 0) {
		printf("vma-test: cleanup split/remap failed\n");
		return -1;
	}

	return 0;
}

static int test_split_enomem_preserves_mapping(void)
{
	volatile char *mem = (volatile char *)BASE_FULL;
	unsigned long fillers[MAX_FILLERS];
	int filler_count = 0;
	int table_full = 0;

	if (expect_mmap(BASE_FULL, 3 * PAGE_SIZE, PROT_READ | PROT_WRITE) < 0)
		return -1;

	for (int i = 0; i < MAX_FILLERS; i++) {
		unsigned long addr = BASE_FULL + VMA_GAP +
				     (unsigned long)i * 2 * PAGE_SIZE;
		void *ret = mmap((void *)addr, PAGE_SIZE, PROT_READ,
				 MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);

		if ((unsigned long)ret == addr) {
			fillers[filler_count++] = addr;
			continue;
		}
		if ((long)ret == -ENOMEM) {
			table_full = 1;
			break;
		}

		printf("vma-test: filler mmap %p failed: %ld\n", (void *)addr,
		       (long)ret);
		return -1;
	}

	if (!table_full) {
		printf("vma-test: did not fill VMA table\n");
		return -1;
	}

	if (filler_count >= NR_VMA) {
		printf("vma-test: filler count is unexpectedly high\n");
		return -1;
	}

	for (int i = 0; i < filler_count; i++) {
		volatile char *filler = (volatile char *)fillers[i];
		if (*filler != 0)
			return -1;
	}

	long ret = munmap((void *)(BASE_FULL + PAGE_SIZE), PAGE_SIZE);
	if (ret != -ENOMEM) {
		printf("vma-test: split with full VMA table returned %ld\n", ret);
		return -1;
	}

	mem[0] = 0x41;
	mem[PAGE_SIZE] = 0x42;
	mem[2 * PAGE_SIZE] = 0x43;
	if (mem[0] != 0x41 || mem[PAGE_SIZE] != 0x42 ||
	    mem[2 * PAGE_SIZE] != 0x43) {
		printf("vma-test: failed split changed original mapping\n");
		return -1;
	}

	(void)munmap((void *)BASE_FULL, 3 * PAGE_SIZE);
	for (int i = 0; i < filler_count; i++)
		(void)munmap((void *)fillers[i], PAGE_SIZE);

	return 0;
}

int main(void)
{
	if (test_adjacent_merge() < 0)
		return 1;
	if (test_split_remap() < 0)
		return 1;
	if (test_split_enomem_preserves_mapping() < 0)
		return 1;

	printf("vma-test: ok\n");
	return 0;
}
