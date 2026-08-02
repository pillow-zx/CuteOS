/* Page-cache and writeback mechanism tests using synthetic storage. */

#include "memory_fixture.h"

#include <kernel/errno.h>
#include <kernel/page_cache.h>
#include <kernel/test.h>
#include <kernel/vfs.h>

#include "../ktest.h"

static void fill_pattern(uint8_t *buf, size_t len, uint8_t seed)
{
	for (size_t i = 0; i < len; i++)
		buf[i] = (uint8_t)(seed + (uint8_t)(i * 13u));
}

static uint64_t page_cache_key_test_block;

static int page_cache_key_test_resolve(struct page_mapping *mapping,
					       uint64_t index, bool create,
					       uint64_t *block)
{
	(void)mapping;
	(void)index;
	(void)create;
	if (!block)
		return -EINVAL;
	*block = page_cache_key_test_block;
	return 0;
}

static const struct page_mapping_ops page_cache_key_test_ops = {
	.resolve = page_cache_key_test_resolve,
};

static int datasync_test_writebacks;
static int datasync_test_hooks;

static int datasync_test_write_inode(struct inode *inode)
{
	(void)inode;
	datasync_test_writebacks++;
	return 0;
}

static int datasync_test_datasync_inode(struct inode *inode)
{
	(void)inode;
	datasync_test_hooks++;
	return 0;
}

static const struct super_operations datasync_fallback_sops = {
	.write_inode = datasync_test_write_inode,
};

static const struct super_operations datasync_hook_sops = {
	.write_inode = datasync_test_write_inode,
	.datasync_inode = datasync_test_datasync_inode,
};

int test_page_cache_dirty_write_visibility(void)
{
	static uint8_t wbuf[BLOCK_SIZE];
	static uint8_t raw[BLOCK_SIZE];
	struct ktest_memory_file file;

	TEST_BEGIN("page cache: dirty write stays off disk");
	{
		TEST_ASSERT_EQ(ktest_memory_file_init(&file,
						     KTEST_MEMORY_DEV(10), 0),
				       0);
		fill_pattern(wbuf, sizeof(wbuf), 0x31);
		memset(raw, 0, sizeof(raw));

		TEST_ASSERT_EQ(vfs_write(&file.file, (const char *)wbuf,
					 sizeof(wbuf)), (ssize_t)sizeof(wbuf));
		TEST_ASSERT_EQ(ktest_memory_file_read_block(&file, 0, raw), 0);
		TEST_ASSERT_NE(memcmp(raw, wbuf, sizeof(wbuf)), 0);
		TEST_ASSERT_EQ(vfs_sync_file(&file.file), 0);
		TEST_ASSERT_EQ(ktest_memory_file_read_block(&file, 0, raw), 0);
		TEST_ASSERT_EQ(memcmp(raw, wbuf, sizeof(wbuf)), 0);
	}
	TEST_END("page cache: dirty write stays off disk");
	ktest_memory_file_destroy(&file);
	return __test_ret;
fail:
	TEST_FAIL("page cache: dirty write stays off disk", "see above");
	ktest_memory_file_destroy(&file);
	return __test_ret;
}

int test_page_cache_physical_key_identity(void)
{
	struct page_mapping first = {0};
	struct page_mapping second = {0};
	struct page_cache *first_page = NULL;
	struct page_cache *second_page = NULL;

	TEST_BEGIN("page cache: physical key identity");
	{
		TEST_ASSERT_EQ(ktest_memory_device_init(), 0);
		page_cache_key_test_block = 7;
		page_mapping_init(&first, &first, KTEST_MEMORY_DEV(11),
				  &page_cache_key_test_ops);
		page_mapping_init(&second, &second, KTEST_MEMORY_DEV(11),
				   &page_cache_key_test_ops);

		first_page = page_cache_get_mapping(&first, 3, PAGE_CACHE_READ,
						    NULL);
		second_page = page_cache_get_mapping(&second, 9, PAGE_CACHE_READ,
						     NULL);
		TEST_ASSERT_NOT_NULL(first_page);
		TEST_ASSERT_EQ(second_page, first_page);
		page_cache_put_page(second_page);
		page_cache_put_page(first_page);
	}
	TEST_END("page cache: physical key identity");
	return __test_ret;
fail:
	TEST_FAIL("page cache: physical key identity", "see above");
	if (second_page)
		page_cache_put_page(second_page);
	if (first_page)
		page_cache_put_page(first_page);
	return __test_ret;
}

int test_page_cache_writeback_retry(void)
{
	static uint8_t pattern[BLOCK_SIZE];
	static uint8_t raw[BLOCK_SIZE];
	struct ktest_memory_file file;
	struct page_cache *page = NULL;

	TEST_BEGIN("page cache: failed writeback retries");
	{
		TEST_ASSERT_EQ(ktest_memory_file_init(&file,
						     KTEST_MEMORY_DEV(12), 0),
				       0);
		fill_pattern(pattern, sizeof(pattern), 0xa4);
		TEST_ASSERT_EQ(vfs_write(&file.file, (const char *)pattern,
					 sizeof(pattern)), (ssize_t)sizeof(pattern));
		page = page_cache_get_mapping(&file.inode.i_pages, 0,
						      PAGE_CACHE_READ, NULL);
		TEST_ASSERT_NOT_NULL(page);

		ktest_memory_device_fail_next_write(-EIO);
		TEST_ASSERT_EQ(page_cache_sync_page(page), -EIO);
		TEST_ASSERT(page_cache_is_dirty(page));
		TEST_ASSERT_EQ(memcmp(page_cache_data(page), pattern,
				      sizeof(pattern)), 0);

		TEST_ASSERT_EQ(page_cache_sync_page(page), 0);
		TEST_ASSERT(!page_cache_is_dirty(page));
		TEST_ASSERT_EQ(ktest_memory_file_read_block(&file, 0, raw), 0);
		TEST_ASSERT_EQ(memcmp(raw, pattern, sizeof(raw)), 0);
	}
	TEST_END("page cache: failed writeback retries");
	if (page)
		page_cache_put_page(page);
	ktest_memory_file_destroy(&file);
	return __test_ret;
fail:
	TEST_FAIL("page cache: failed writeback retries", "see above");
	ktest_memory_device_fail_next_write(0);
	if (page)
		page_cache_put_page(page);
	ktest_memory_file_destroy(&file);
	return __test_ret;
}

int test_page_cache_fsync_inode_scope(void)
{
	static uint8_t abuf[BLOCK_SIZE];
	static uint8_t bbuf[BLOCK_SIZE];
	static uint8_t raw[BLOCK_SIZE];
	struct ktest_memory_file file_a;
	struct ktest_memory_file file_b;

	TEST_BEGIN("page cache: fsync flushes one inode");
	{
		TEST_ASSERT_EQ(ktest_memory_file_init(&file_a,
						     KTEST_MEMORY_DEV(13), 0),
				       0);
		TEST_ASSERT_EQ(ktest_memory_file_init(&file_b,
						     KTEST_MEMORY_DEV(14), 1024),
				       0);
		fill_pattern(abuf, sizeof(abuf), 0x51);
		fill_pattern(bbuf, sizeof(bbuf), 0x91);

		TEST_ASSERT_EQ(vfs_write(&file_a.file, (const char *)abuf,
					 sizeof(abuf)), (ssize_t)sizeof(abuf));
		TEST_ASSERT_EQ(vfs_write(&file_b.file, (const char *)bbuf,
					 sizeof(bbuf)), (ssize_t)sizeof(bbuf));

		memset(raw, 0, sizeof(raw));
		TEST_ASSERT_EQ(ktest_memory_file_read_block(&file_a, 0, raw), 0);
		TEST_ASSERT_NE(memcmp(raw, abuf, sizeof(abuf)), 0);
		TEST_ASSERT_EQ(ktest_memory_file_read_block(&file_b, 0, raw), 0);
		TEST_ASSERT_NE(memcmp(raw, bbuf, sizeof(bbuf)), 0);

		TEST_ASSERT_EQ(vfs_sync_file(&file_a.file), 0);
		TEST_ASSERT_EQ(ktest_memory_file_read_block(&file_a, 0, raw), 0);
		TEST_ASSERT_EQ(memcmp(raw, abuf, sizeof(abuf)), 0);
		TEST_ASSERT_EQ(ktest_memory_file_read_block(&file_b, 0, raw), 0);
		TEST_ASSERT_NE(memcmp(raw, bbuf, sizeof(bbuf)), 0);
	}
	TEST_END("page cache: fsync flushes one inode");
	ktest_memory_file_destroy(&file_a);
	ktest_memory_file_destroy(&file_b);
	return __test_ret;
fail:
	TEST_FAIL("page cache: fsync flushes one inode", "see above");
	ktest_memory_file_destroy(&file_a);
	ktest_memory_file_destroy(&file_b);
	return __test_ret;
}

int test_vfs_datasync_metadata_policy(void)
{
	struct super_block sb = {0};
	struct inode inode = {0};
	struct file file = {0};

	TEST_BEGIN("vfs: fdatasync metadata hook policy");
	{
		inode.i_sb = &sb;
		file.f_inode = &inode;
		page_mapping_init(&inode.i_pages, &inode, 0, NULL);

		sb.s_op = &datasync_fallback_sops;
		datasync_test_writebacks = 0;
		datasync_test_hooks = 0;
		TEST_ASSERT_EQ(vfs_datasync_file(&file), 0);
		TEST_ASSERT_EQ(datasync_test_writebacks, 1);
		TEST_ASSERT_EQ(datasync_test_hooks, 0);

		sb.s_op = &datasync_hook_sops;
		datasync_test_writebacks = 0;
		datasync_test_hooks = 0;
		TEST_ASSERT_EQ(vfs_datasync_file(&file), 0);
		TEST_ASSERT_EQ(datasync_test_writebacks, 0);
		TEST_ASSERT_EQ(datasync_test_hooks, 1);
	}
	TEST_END("vfs: fdatasync metadata hook policy");
	return __test_ret;
fail:
	TEST_FAIL("vfs: fdatasync metadata hook policy", "see above");
	return __test_ret;
}

int test_page_cache_datasync_skips_pure_inode_metadata(void)
{
	static uint8_t data[BLOCK_SIZE];
	static uint8_t raw[BLOCK_SIZE];
	struct ktest_memory_file file;

	TEST_BEGIN("page cache: fdatasync flushes data and uses hook");
	{
		TEST_ASSERT_EQ(ktest_memory_file_init(&file,
						     KTEST_MEMORY_DEV(15), 0),
				       0);
		file.sb.s_op = &datasync_hook_sops;
		datasync_test_writebacks = 0;
		datasync_test_hooks = 0;
		fill_pattern(data, sizeof(data), 0xb5);
		TEST_ASSERT_EQ(vfs_write(&file.file, (const char *)data,
					 sizeof(data)), (ssize_t)sizeof(data));
		TEST_ASSERT_EQ(ktest_memory_file_read_block(&file, 0, raw), 0);
		TEST_ASSERT_NE(memcmp(raw, data, sizeof(data)), 0);

		TEST_ASSERT_EQ(vfs_datasync_file(&file.file), 0);
		TEST_ASSERT_EQ(datasync_test_hooks, 1);
		TEST_ASSERT_EQ(datasync_test_writebacks, 0);
		TEST_ASSERT_EQ(ktest_memory_file_read_block(&file, 0, raw), 0);
		TEST_ASSERT_EQ(memcmp(raw, data, sizeof(data)), 0);
	}
	TEST_END("page cache: fdatasync flushes data and uses hook");
	ktest_memory_file_destroy(&file);
	return __test_ret;
fail:
	TEST_FAIL("page cache: fdatasync flushes data and uses hook", "see above");
	ktest_memory_file_destroy(&file);
	return __test_ret;
}

int test_page_cache_raw_alias_fsync(void)
{
	static uint8_t data[BLOCK_SIZE];
	static uint8_t raw[BLOCK_SIZE];
	struct ktest_memory_file file;
	struct page_cache *raw_page = NULL;

	TEST_BEGIN("page cache: block mapping aliases file mapping");
	{
		TEST_ASSERT_EQ(ktest_memory_file_init(&file,
						     KTEST_MEMORY_DEV(16), 0),
				       0);
		fill_pattern(data, sizeof(data), 0x73);
		TEST_ASSERT_EQ(vfs_write(&file.file, (const char *)data,
					 sizeof(data)), (ssize_t)sizeof(data));
		raw_page = page_cache_get_block(file.dev, 0);
		TEST_ASSERT_NOT_NULL(raw_page);
		TEST_ASSERT_EQ(memcmp(page_cache_data(raw_page), data,
				      sizeof(data)), 0);
		TEST_ASSERT_EQ(vfs_sync_file(&file.file), 0);
		TEST_ASSERT_EQ(ktest_memory_file_read_block(&file, 0, raw), 0);
		TEST_ASSERT_EQ(memcmp(raw, data, sizeof(raw)), 0);
	}
	TEST_END("page cache: block mapping aliases file mapping");
	if (raw_page)
		page_cache_put_page(raw_page);
	ktest_memory_file_destroy(&file);
	return __test_ret;
fail:
	TEST_FAIL("page cache: block mapping aliases file mapping", "see above");
	if (raw_page)
		page_cache_put_page(raw_page);
	ktest_memory_file_destroy(&file);
	return __test_ret;
}

int test_page_cache_raw_alias_drop(void)
{
	static uint8_t old_data[BLOCK_SIZE];
	static uint8_t new_data[BLOCK_SIZE];
	struct ktest_memory_file file;
	struct page_cache *page = NULL;

	TEST_BEGIN("page cache: mapping invalidation reloads raw alias");
	{
		TEST_ASSERT_EQ(ktest_memory_file_init(&file,
						     KTEST_MEMORY_DEV(17), 0),
				       0);
		fill_pattern(old_data, sizeof(old_data), 0x19);
		fill_pattern(new_data, sizeof(new_data), 0xe3);
		TEST_ASSERT_EQ(ktest_memory_file_seed_block(&file, 0, old_data), 0);

		page = page_cache_get_mapping(&file.inode.i_pages, 0,
					      PAGE_CACHE_READ, NULL);
		TEST_ASSERT_NOT_NULL(page);
		TEST_ASSERT_EQ(memcmp(page_cache_data(page), old_data,
				      sizeof(old_data)), 0);
		page_cache_put_page(page);
		page = NULL;

		TEST_ASSERT_EQ(ktest_memory_file_write_block(&file, 0, new_data),
				       0);
		page_cache_invalidate_mapping(&file.inode.i_pages);
		page = page_cache_get_mapping(&file.inode.i_pages, 0,
					      PAGE_CACHE_READ, NULL);
		TEST_ASSERT_NOT_NULL(page);
		TEST_ASSERT_EQ(memcmp(page_cache_data(page), new_data,
				      sizeof(new_data)), 0);
	}
	TEST_END("page cache: mapping invalidation reloads raw alias");
	if (page)
		page_cache_put_page(page);
	ktest_memory_file_destroy(&file);
	return __test_ret;
fail:
	TEST_FAIL("page cache: mapping invalidation reloads raw alias",
		  "see above");
	if (page)
		page_cache_put_page(page);
	ktest_memory_file_destroy(&file);
	return __test_ret;
}

int test_page_cache_pressure_eviction(void)
{
	enum { NR_PRESSURE_PAGES = 513 };
	static uint8_t page_buf[BLOCK_SIZE];
	static uint8_t raw[BLOCK_SIZE];
	struct ktest_memory_file dirty_file;
	struct ktest_memory_file clean_file;

	TEST_BEGIN("page cache: pressure eviction and progress");
	{
		TEST_ASSERT_EQ(ktest_memory_file_init(&dirty_file,
						     KTEST_MEMORY_DEV(18), 0),
				       0);
		for (uint32_t i = 0; i < NR_PRESSURE_PAGES; i++) {
			fill_pattern(page_buf, sizeof(page_buf), (uint8_t)i);
			dirty_file.file.f_pos = (loff_t)i * BLOCK_SIZE;
			TEST_ASSERT_EQ(vfs_write(&dirty_file.file,
						 (const char *)page_buf,
						 sizeof(page_buf)),
				       (ssize_t)sizeof(page_buf));
		}

		fill_pattern(page_buf, sizeof(page_buf), 0);
		memset(raw, 0, sizeof(raw));
		TEST_ASSERT_EQ(ktest_memory_file_read_block(&dirty_file, 0, raw),
				       0);
		TEST_ASSERT_EQ(memcmp(raw, page_buf, sizeof(page_buf)), 0);
		TEST_ASSERT_EQ(vfs_sync_file(&dirty_file.file), 0);

		TEST_ASSERT_EQ(ktest_memory_file_init(&clean_file,
						     KTEST_MEMORY_DEV(19), 1024),
				       0);
		fill_pattern(page_buf, sizeof(page_buf), 0xa7);
		TEST_ASSERT_EQ(vfs_write(&clean_file.file, (const char *)page_buf,
					 sizeof(page_buf)), (ssize_t)sizeof(page_buf));
		memset(raw, 0, sizeof(raw));
		TEST_ASSERT_EQ(ktest_memory_file_read_block(&clean_file, 0, raw),
				       0);
		TEST_ASSERT_NE(memcmp(raw, page_buf, sizeof(page_buf)), 0);
	}
	TEST_END("page cache: pressure eviction and progress");
	ktest_memory_file_destroy(&clean_file);
	ktest_memory_file_destroy(&dirty_file);
	return __test_ret;
fail:
	TEST_FAIL("page cache: pressure eviction and progress", "see above");
	ktest_memory_file_destroy(&clean_file);
	ktest_memory_file_destroy(&dirty_file);
	return __test_ret;
}

int test_page_cache_clustered_writeback(void)
{
	static uint8_t page_buf[BLOCK_SIZE];
	struct ktest_memory_file file;
	struct ktest_memory_stats stats;

	TEST_BEGIN("page cache: clustered writeback");
	{
		TEST_ASSERT_EQ(ktest_memory_file_init(&file,
						     KTEST_MEMORY_DEV(20), 0),
				       0);
		for (uint32_t i = 0; i < 3; i++) {
			fill_pattern(page_buf, sizeof(page_buf), (uint8_t)(0xc0 + i));
			file.file.f_pos = (loff_t)i * BLOCK_SIZE;
			TEST_ASSERT_EQ(vfs_write(&file.file, (const char *)page_buf,
						 sizeof(page_buf)),
				       (ssize_t)sizeof(page_buf));
		}

		ktest_memory_device_reset_stats();
		TEST_ASSERT_EQ(vfs_sync_file(&file.file), 0);
		memset(&stats, 0, sizeof(stats));
		ktest_memory_device_get_stats(&stats);
		TEST_ASSERT_EQ(stats.read_reqs, 0u);
		TEST_ASSERT(stats.write_reqs >= 1);
		TEST_ASSERT(stats.max_write_nsec >= 3 * BLOCK_SECTORS);
	}
	TEST_END("page cache: clustered writeback");
	ktest_memory_file_destroy(&file);
	return __test_ret;
fail:
	TEST_FAIL("page cache: clustered writeback", "see above");
	ktest_memory_file_destroy(&file);
	return __test_ret;
}

int test_page_cache_indirect_reclaim_progress(void)
{
	enum {
		NR_INDIRECT_PAGES = 513,
		START_INDEX = 12,
	};
	static uint8_t page_buf[BLOCK_SIZE];
	static uint8_t raw[BLOCK_SIZE];
	struct ktest_memory_file file;

	TEST_BEGIN("page cache: indirect reclaim progress");
	{
		TEST_ASSERT_EQ(ktest_memory_file_init(&file,
						     KTEST_MEMORY_DEV(21), 0),
				       0);
		file.file.f_pos = (loff_t)START_INDEX * BLOCK_SIZE;
		for (uint32_t i = 0; i < NR_INDIRECT_PAGES; i++) {
			fill_pattern(page_buf, sizeof(page_buf), (uint8_t)(0x20 + i));
			TEST_ASSERT_EQ(vfs_write(&file.file, (const char *)page_buf,
						 sizeof(page_buf)),
				       (ssize_t)sizeof(page_buf));
		}

		TEST_ASSERT_EQ(vfs_sync_file(&file.file), 0);
		fill_pattern(page_buf, sizeof(page_buf), 0x20);
		memset(raw, 0, sizeof(raw));
		TEST_ASSERT_EQ(ktest_memory_file_read_block(&file, START_INDEX,
							   raw), 0);
		TEST_ASSERT_EQ(memcmp(raw, page_buf, sizeof(page_buf)), 0);
	}
	TEST_END("page cache: indirect reclaim progress");
	ktest_memory_file_destroy(&file);
	return __test_ret;
fail:
	TEST_FAIL("page cache: indirect reclaim progress", "see above");
	ktest_memory_file_destroy(&file);
	return __test_ret;
}

int test_page_cache_large_offset_rejected(void)
{
	static uint8_t byte = 0x5a;
	struct ktest_memory_file file;

	TEST_BEGIN("page cache: large offset rejected");
	{
		TEST_ASSERT_EQ(ktest_memory_file_init(&file,
						     KTEST_MEMORY_DEV(22), 0),
				       0);
		file.file.f_pos = (loff_t)UINT32_MAX + 1;
		TEST_ASSERT_EQ(vfs_write(&file.file, (const char *)&byte, 1),
				       (ssize_t)-EFBIG);
		TEST_ASSERT_EQ(file.inode.i_size, 0ULL);
	}
	TEST_END("page cache: large offset rejected");
	ktest_memory_file_destroy(&file);
	return __test_ret;
fail:
	TEST_FAIL("page cache: large offset rejected", "see above");
	ktest_memory_file_destroy(&file);
	return __test_ret;
}
