#include <kernel/hash.h>
#include <kernel/test.h>

#include "ktest.h"

struct hash_test_node {
	uint64_t key;
	struct list_head hash;
};

void test_hash_insert_lookup(void)
{
	TEST_BEGIN("hash: insert lookup");
	{
		HASH_TABLE_DECLARE_STATIC(table, 2);
		struct hash_test_node node = {
			.key = 0x1234,
		};
		struct list_head *pos;
		bool found_same_hash = false;
		bool found_other_hash = false;

		INIT_LIST_HEAD(&node.hash);
		hash_table_init(&table);
		hash_table_add(&table, node.key, &node.hash);

		hash_table_for_each_possible (pos, &table, node.key) {
			struct hash_test_node *entry;

			entry = list_entry(pos, struct hash_test_node, hash);
			if (entry->key == node.key)
				found_same_hash = true;
		}

		hash_table_for_each_possible (pos, &table, node.key + 1) {
			struct hash_test_node *entry;

			entry = list_entry(pos, struct hash_test_node, hash);
			if (entry->key == node.key)
				found_other_hash = true;
		}

		TEST_ASSERT_EQ(found_same_hash, true);
		TEST_ASSERT_EQ(found_other_hash, false);
	}
	TEST_END("hash: insert lookup");
	return;
fail:
	TEST_FAIL("hash: insert lookup", "see above");
}

void test_hash_collision_delete(void)
{
	TEST_BEGIN("hash: collision delete");
	{
		HASH_TABLE_DECLARE_STATIC(table, 2);
		struct hash_test_node first = {
			.key = 0x10,
		};
		struct hash_test_node second = {
			.key = 0x20,
		};
		struct list_head *pos;
		uint32_t found = 0;

		INIT_LIST_HEAD(&first.hash);
		INIT_LIST_HEAD(&second.hash);
		hash_table_init(&table);
		hash_table_add(&table, first.key, &first.hash);
		hash_table_add(&table, second.key, &second.hash);

		hash_table_for_each_possible (pos, &table, first.key) {
			struct hash_test_node *entry;

			entry = list_entry(pos, struct hash_test_node, hash);
			if (entry->key == first.key)
				found |= 0x1;
			if (entry->key == second.key)
				found |= 0x2;
		}
		TEST_ASSERT_EQ(found, (uint32_t)0x3);

		hash_table_del(&first.hash);

		found = 0;
		hash_table_for_each_possible (pos, &table, first.key) {
			struct hash_test_node *entry;

			entry = list_entry(pos, struct hash_test_node, hash);
			if (entry->key == first.key)
				found |= 0x1;
			if (entry->key == second.key)
				found |= 0x2;
		}
		TEST_ASSERT_EQ(found, (uint32_t)0x2);
	}
	TEST_END("hash: collision delete");
	return;
fail:
	TEST_FAIL("hash: collision delete", "see above");
}
