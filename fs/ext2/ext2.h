#ifndef _CUTEOS_FS_EXT2_EXT2_H
#define _CUTEOS_FS_EXT2_EXT2_H

/*
 * fs/ext2/ext2.h - EXT2 磁盘格式定义
 *
 * 功能：
 *   定义 EXT2 文件系统的磁盘数据结构，包括超级块、块组描述符、
 *   inode、目录项等。这些结构体直接对应磁盘上的二进制布局，
 *   字段顺序和位宽必须严格与 EXT2 规范一致。仅在此目录内使用，
 *   不对外暴露。
 *
 * 主要定义：
 *   struct ext2_super_block         - 超级块（inodes_count, blocks_count,
 *                                     first_data_block, log_block_size,
 *                                     log_frag_size, blocks_per_group,
 *                                     magic（0xEF53）等）
 *   struct ext2_group_desc          - 块组描述符（bg_block_bitmap,
 *                                     bg_inode_bitmap, bg_inode_table,
 *                                     bg_free_blocks_count 等）
 *   struct ext2_inode               - inode 磁盘格式（i_mode, i_uid,
 *                                     i_size, i_atime, i_block[15] 等）
 *   struct ext2_dir_entry_2         - 目录项（inode, rec_len,
 *                                     name_len, file_type, name[]）
 *   EXT2_SUPER_MAGIC               - EXT2 魔数（0xEF53）
 *   EXT2_ROOT_INO                  - 根 inode 号（2）
 *   EXT2_GOOD_OLD_REVISION         - EXT2 修订版本
 *   EXT2_NAME_LEN                  - 最大文件名长度
 *   EXT2_NDIR_BLOCKS / EXT2_IND_BLOCK / EXT2_DIND_BLOCK / EXT2_TIND_BLOCK
 *                                   - 直接/间接块索引
 */

#include <kernel/buffer.h>
#include <kernel/compiler.h>
#include <kernel/fs.h>
#include <kernel/types.h>

#define EXT2_SUPER_MAGIC	 0xef53
#define EXT2_ROOT_INO		 2
#define EXT2_GOOD_OLD_REV	 0
#define EXT2_DYNAMIC_REV	 1
#define EXT2_GOOD_OLD_INODE_SIZE 128
#define EXT2_NAME_LEN		 255

#define EXT2_NDIR_BLOCKS 12
#define EXT2_IND_BLOCK	 12
#define EXT2_DIND_BLOCK	 13
#define EXT2_TIND_BLOCK	 14
#define EXT2_N_BLOCKS	 15

#define EXT2_FT_UNKNOWN	 0
#define EXT2_FT_REG_FILE 1
#define EXT2_FT_DIR	 2
#define EXT2_FT_CHRDEV	 3
#define EXT2_FT_BLKDEV	 4
#define EXT2_FT_FIFO	 5
#define EXT2_FT_SOCK	 6
#define EXT2_FT_SYMLINK	 7

#define EXT2_S_IFMT   0xf000
#define EXT2_S_IFSOCK 0xc000
#define EXT2_S_IFLNK  0xa000
#define EXT2_S_IFREG  0x8000
#define EXT2_S_IFBLK  0x6000
#define EXT2_S_IFDIR  0x4000
#define EXT2_S_IFCHR  0x2000
#define EXT2_S_IFIFO  0x1000

struct ext2_super_block {
	uint32_t s_inodes_count;
	uint32_t s_blocks_count;
	uint32_t s_r_blocks_count;
	uint32_t s_free_blocks_count;
	uint32_t s_free_inodes_count;
	uint32_t s_first_data_block;
	uint32_t s_log_block_size;
	uint32_t s_log_frag_size;
	uint32_t s_blocks_per_group;
	uint32_t s_frags_per_group;
	uint32_t s_inodes_per_group;
	uint32_t s_mtime;
	uint32_t s_wtime;
	uint16_t s_mnt_count;
	uint16_t s_max_mnt_count;
	uint16_t s_magic;
	uint16_t s_state;
	uint16_t s_errors;
	uint16_t s_minor_rev_level;
	uint32_t s_lastcheck;
	uint32_t s_checkinterval;
	uint32_t s_creator_os;
	uint32_t s_rev_level;
	uint16_t s_def_resuid;
	uint16_t s_def_resgid;
	uint32_t s_first_ino;
	uint16_t s_inode_size;
	uint16_t s_block_group_nr;
	uint32_t s_feature_compat;
	uint32_t s_feature_incompat;
	uint32_t s_feature_ro_compat;
	uint8_t s_uuid[16];
	char s_volume_name[16];
	char s_last_mounted[64];
	uint32_t s_algorithm_usage_bitmap;
	uint8_t s_prealloc_blocks;
	uint8_t s_prealloc_dir_blocks;
	uint16_t s_padding1;
	uint8_t s_journal_uuid[16];
	uint32_t s_journal_inum;
	uint32_t s_journal_dev;
	uint32_t s_last_orphan;
	uint32_t s_hash_seed[4];
	uint8_t s_def_hash_version;
	uint8_t s_reserved_char_pad;
	uint16_t s_reserved_word_pad;
	uint32_t s_default_mount_opts;
	uint32_t s_first_meta_bg;
	uint32_t s_reserved[190];
} __packed;

struct ext2_group_desc {
	uint32_t bg_block_bitmap;
	uint32_t bg_inode_bitmap;
	uint32_t bg_inode_table;
	uint16_t bg_free_blocks_count;
	uint16_t bg_free_inodes_count;
	uint16_t bg_used_dirs_count;
	uint16_t bg_pad;
	uint32_t bg_reserved[3];
} __packed;

struct ext2_inode {
	uint16_t i_mode;
	uint16_t i_uid;
	uint32_t i_size;
	uint32_t i_atime;
	uint32_t i_ctime;
	uint32_t i_mtime;
	uint32_t i_dtime;
	uint16_t i_gid;
	uint16_t i_links_count;
	uint32_t i_blocks;
	uint32_t i_flags;
	uint32_t i_osd1;
	uint32_t i_block[EXT2_N_BLOCKS];
	uint32_t i_generation;
	uint32_t i_file_acl;
	uint32_t i_dir_acl;
	uint32_t i_faddr;
	uint8_t i_osd2[12];
} __packed;

struct ext2_dir_entry_2 {
	uint32_t inode;
	uint16_t rec_len;
	uint8_t name_len;
	uint8_t file_type;
	char name[];
} __packed;

struct ext2_sb_info {
	struct ext2_super_block s_es;
	struct ext2_group_desc *s_group_desc;
	uint32_t s_groups_count;
	uint32_t s_inode_size;
	uint32_t s_inodes_per_group;
	uint32_t s_blocks_per_group;
	uint32_t s_first_data_block;
};

struct ext2_inode_info {
	struct ext2_inode raw_inode;
};

extern const struct inode_operations ext2_dir_inode_operations;
extern const struct inode_operations ext2_symlink_inode_operations;
extern const struct file_operations ext2_dir_operations;
extern const struct file_operations ext2_file_operations;

static inline struct ext2_sb_info *EXT2_SB(struct super_block *sb)
{
	return (struct ext2_sb_info *)sb->s_private;
}

static inline struct ext2_inode_info *EXT2_I(struct inode *inode)
{
	return (struct ext2_inode_info *)inode->i_private;
}

int ext2_init(void);
int mount_root(void);

int ext2_read_inode(struct inode *inode);
int ext2_write_inode(struct inode *inode);
uint32_t ext2_bmap(struct inode *inode, uint32_t block, bool create);

uint32_t ext2_alloc_block(struct inode *inode);
void ext2_free_block(struct super_block *sb, uint32_t block);
uint32_t ext2_alloc_inode(struct super_block *sb, uint16_t mode);
void ext2_free_inode(struct super_block *sb, uint32_t ino);

ssize_t ext2_read_file(struct inode *inode, char *buf, size_t count,
		       loff_t pos);
ssize_t ext2_write_file(struct inode *inode, const char *buf, size_t count,
			loff_t pos);

#endif
