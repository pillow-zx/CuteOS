#ifndef _CUTEOS_TEST_MEMORY_FIXTURE_H
#define _CUTEOS_TEST_MEMORY_FIXTURE_H

#include <kernel/blkdev.h>
#include <kernel/fs.h>
#include <kernel/types.h>

constexpr uint32_t KTEST_MEMORY_MAJOR = 27u;
constexpr uint32_t KTEST_MEMORY_BLOCKS = 2048u;
constexpr uint32_t KTEST_MEMORY_FILE_BLOCKS = 1024u;

#define KTEST_MEMORY_DEV(minor) MKDEV(KTEST_MEMORY_MAJOR, (minor))

struct ktest_memory_stats {
	uint32_t read_reqs;
	uint32_t write_reqs;
	uint32_t max_write_nsec;
	uint32_t last_write_nsec;
};

struct ktest_memory_file {
	struct super_block sb;
	struct inode inode;
	struct file file;
	dev_t dev;
	uint32_t first_block;
	bool allocated[KTEST_MEMORY_FILE_BLOCKS];
};

int ktest_memory_device_init(void);
void ktest_memory_device_clear(void);
void ktest_memory_device_reset_stats(void);
void ktest_memory_device_get_stats(struct ktest_memory_stats *stats);
void ktest_memory_device_fail_next_read(int error);
void ktest_memory_device_fail_next_write(int error);

int ktest_memory_file_init(struct ktest_memory_file *file, dev_t dev,
			   uint32_t first_block);
void ktest_memory_file_destroy(struct ktest_memory_file *file);
int ktest_memory_file_seed_block(struct ktest_memory_file *file,
				uint32_t index, const void *data);
int ktest_memory_file_read_block(const struct ktest_memory_file *file,
				uint32_t index, void *data);
int ktest_memory_file_write_block(struct ktest_memory_file *file,
				 uint32_t index, const void *data);

#endif
