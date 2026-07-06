# ext2 文件系统架构

ext2 是 cuteOS 当前根文件系统实现。它把磁盘格式、inode/block 分配、目录项和文件数据块映射封装在 `fs/ext2/` 内部，并通过 VFS 操作向量对外暴露。

## 代码边界

主要文件：

- `fs/ext2/ext2.h`：ext2 私有磁盘格式和内部 API。
- `fs/ext2/super.c`：注册、mount、super block、statfs、root inode。
- `fs/ext2/inode.c`：inode 读写、block mapping、truncate、inode ops 初始化。
- `fs/ext2/file.c`：普通文件读写和 inode page_mapping ops。
- `fs/ext2/dir.c`：目录查找、目录项变更、symlink、mkdir/link/unlink/rename。
- `fs/ext2/balloc.c`：块位图和 inode 位图分配/释放。

`fs/ext2/ext2.h` 不对 VFS 外部暴露。上层只通过 `struct inode_operations`、`struct file_operations` 和 `struct super_operations` 调用 ext2。

## 磁盘格式结构

ext2 私有头定义与磁盘布局一致的 packed 结构：

- `struct ext2_super_block`
- `struct ext2_group_desc`
- `struct ext2_inode`
- `struct ext2_dir_entry_2`

关键常量：

```c
#define EXT2_SUPER_MAGIC 0xef53
#define EXT2_ROOT_INO 2
#define EXT2_SUPER_OFFSET 1024u
#define EXT2_NAME_LEN 255
#define EXT2_NDIR_BLOCKS 12
#define EXT2_IND_BLOCK 12
#define EXT2_DIND_BLOCK 13
#define EXT2_TIND_BLOCK 14
#define EXT2_N_BLOCKS 15
```

当前文件系统块大小固定为 `BLOCK_SIZE=4096`。mount 时若 ext2 super block 的 block size 不是 4 KiB，会拒绝挂载。

## super block 私有状态

`struct ext2_sb_info` 挂在 `super_block.s_private`：

```c
struct ext2_sb_info {
    struct ext2_super_block s_es;
    struct ext2_group_desc *s_group_desc;
    uint32_t s_groups_count;
    uint32_t s_inode_size;
    uint32_t s_inodes_per_group;
    uint32_t s_blocks_per_group;
    uint32_t s_first_data_block;
};
```

block group descriptor table 通过 `vmalloc()` 分配并读入内存。metadata block 的读取使用块设备 page cache：

```c
page_cache_get_block(sb->s_dev, block)
```

## mount 流程

`mount_root()` 当前挂载：

```mermaid
flowchart TD
    Root["mount_root()"]
    Type["get/register filesystem type 'ext2'"]
    Mount["ext2_mount(dev 8:0)"]
    SB["super_alloc() + ext2_read_super()"]
    BGDT["read block group descriptors"]
    Inode["iget(root inode 2)"]
    Dentry["create root dentry"]
    VFS["vfs_set_root_dentry()<br/>vfs_mount_root()"]

    Root --> Type --> Mount --> SB --> BGDT --> Inode --> Dentry --> VFS
```

```c
#define EXT2_ROOT_DEV MKDEV(8, 0)
```

流程：

1. 查找或注册 `"ext2"` 文件系统类型。
2. 调用 `ext2_mount(fs_type, EXT2_ROOT_DEV, NULL)`。
3. `super_alloc()` 分配 VFS super block。
4. `ext2_read_super()` 读取并校验 ext2 super block。
5. `ext2_read_bgdt()` 读取 block group descriptor table。
6. 分配 root dentry。
7. `iget(sb, EXT2_ROOT_INO)` 读取 root inode。
8. 设置 `sb->s_root`。
9. `vfs_set_root_dentry()` 和 `vfs_mount_root()`。

挂载成功后，VFS root 指向 ext2 根目录。

## inode 读写

`ext2_inode_location()` 通过 inode number 定位磁盘 inode：

```text
group = (ino - 1) / inodes_per_group
index = (ino - 1) % inodes_per_group
byte_offset = index * inode_size
block = bg_inode_table + byte_offset / BLOCK_SIZE
offset = byte_offset % BLOCK_SIZE
```

`ext2_read_inode()` 从 inode table block 复制 raw inode 到 `ext2_inode_info`，再填充 VFS inode 字段：

- mode/uid/gid/nlink/size/blocks/timestamps
- 设备特殊文件的 `i_rdev`
- `i_op/i_fop/i_pages.ops/i_pages.backing`

`ext2_write_inode()` 反向把 VFS inode 状态写回 raw inode，并同步 metadata page。

## inode operation 初始化

`ext2_init_inode_ops()` 根据 inode mode 设置操作向量：

| 类型 | i_op | i_fop | i_pages |
| --- | --- | --- | --- |
| directory | `ext2_dir_inode_operations` | `ext2_dir_operations` | `ext2_inode_aops` |
| symlink | `ext2_symlink_inode_operations` | none | `ext2_inode_aops` |
| char/block device | none | none | none |
| regular/default | `ext2_file_inode_operations` | `ext2_file_operations` | `ext2_inode_aops` |

目录和块后备 symlink 在 page cache 看来都是 inode 数据页。设备特殊文件不通过 inode page cache 暴露数据。

`inode->i_pages.backing` 指向底层块设备 mapping，用于 page cache raw block alias coherency。

## 文件数据路径

普通文件读写位于 `fs/ext2/file.c`。

```mermaid
flowchart LR
    VFS["VFS file op"]
    InodeMap["inode->i_pages<br/>logical block index"]
    Aops["ext2_inode_aops"]
    Bmap["ext2_bmap()<br/>direct / indirect"]
    BlockMap["block_device_pages(dev)<br/>physical block index"]
    Driver["block_device_operations<br/>virtio-blk sectors"]

    VFS --> InodeMap --> Aops --> Bmap --> BlockMap --> Driver
```

读取：

1. 根据 `pos/count` 限制到 `inode->i_size`。
2. 按文件逻辑块号 `lblock = pos / BLOCK_SIZE` 循环。
3. `page_cache_read_page(&inode->i_pages, lblock)`。
4. 从 page cache 数据拷贝到调用者缓冲区。

写入：

1. 限制到 `EXT2_MAX_FILE_SIZE`。
2. `page_cache_grab_file_page(inode, lblock, true, &created)`。
3. 通过 `map_block(false)` 判断逻辑块是否已有物理块。
4. 空洞写入时 `map_block(true)` 分配物理块。
5. 部分覆盖已有块时，先 readpage 保留未覆盖字节。
6. 写入 page cache，标 dirty。
7. 如果扩大文件，更新 `inode->i_size` 并 `ext2_write_inode()`。

写入不是每次都同步数据页；dirty page 由 fsync、truncate、msync 或后台 writeback 写回。

## page_mapping ops

ext2 inode mapping 操作：

```c
const struct page_mapping_ops ext2_inode_aops = {
    .readpage = ext2_readpage,
    .map_block = ext2_map_block,
    .writepages = ext2_writepages,
};
```

语义：

- `index` 是文件逻辑块号。
- `readpage()` 通过 `ext2_bmap_readonly()` 找物理块；空洞读返回全零页。
- `map_block(create=false)` 查询已有映射。
- `map_block(create=true)` 允许分配新块。
- `writepages()` 假设调用者已经确认连续逻辑页对应连续物理块，发起一次连续块设备写。

page cache 因此不需要知道 ext2 direct/indirect 结构。

## block mapping

ext2 inode 的 `i_block[15]` 包含：

- 12 个 direct block。
- 1 个 single indirect。
- 1 个 double indirect。
- 1 个 triple indirect 槽位。

当前 `ext2_bmap()` 的文件数据映射支持 direct、single indirect 和 double indirect。truncate/free 路径包含 triple indirect chain 的释放逻辑，但正常 bmap 在 double indirect 范围外返回 0。

`ext2_bmap(inode, block, create)`：

- `create=false` 时只查询。
- `create=true` 时必要时分配数据块和间接块。
- 分配元数据块后会清零并同步。
- 返回物理块号，失败返回 0。

`ext2_map_block()` 对 page cache 把 0 转换成 errno：

- create 模式下无块 -> `-ENOSPC`
- readonly 无块 -> `-EIO`

这样避免用物理块 0 同时表示有效块和失败。

## 目录结构

目录内容是 inode 数据块中的 `struct ext2_dir_entry_2` 序列。目录操作通过 `inode->i_pages` 读写，避免同一个磁盘块同时存在 inode mapping 和 raw block mapping 两个权威副本。

```mermaid
flowchart TD
    DirInode["directory inode"]
    Page["inode page cache page<br/>logical directory block"]
    Records["ext2_dir_entry_2 records"]
    Lookup["lookup: scan rec_len"]
    Add["add: reuse hole / split spare / append block"]
    Delete["delete: merge rec_len or clear inode"]
    Sync["mark dirty + sync page"]

    DirInode --> Page --> Records
    Records --> Lookup
    Records --> Add --> Sync
    Records --> Delete --> Sync
```

查找：

1. 遍历目录大小覆盖的逻辑块。
2. 跳过未映射空洞。
3. 读取目录 page。
4. 按 `rec_len` 扫描 dirent。
5. 匹配 `name_len` 和 name。

新增目录项：

- 优先复用 inode 为 0 的空洞 dirent。
- 或拆分已有 dirent 的 spare 空间。
- 或分配新目录块。
- 目录更新当前同步写回。

删除目录项：

- 若有前驱 dirent，则把前驱 `rec_len` 扩展覆盖当前项。
- 若当前项是块内第一个，则把 `inode` 置 0。

目录变更后同步 page cache page，并更新 inode。

## 创建、链接和重命名

`dir.c` 通过 VFS inode operations 实现：

- `lookup`
- `create`
- `symlink`
- `link`
- `unlink`
- `mkdir`
- `rmdir`
- `readlink`
- `rename`

新 inode 分配由 `ext2_alloc_inode()` 完成，目录项插入失败时通过 rollback 清理新 inode。

link/unlink 修改 `i_nlink` 并写回 inode。`i_nlink` 降为 0 的 inode 在 evict 时释放数据块和 inode bitmap。

rename 需要处理跨目录、替换目标、目录链接数等情况，并保持 dentry cache 与磁盘目录项一致。

## 块和 inode 分配

`fs/ext2/balloc.c` 管理 block bitmap 和 inode bitmap。分配步骤通常是：

1. 遍历 block group descriptor。
2. 读取 bitmap block。
3. 找空闲 bit。
4. 设置 bit 并同步 bitmap page。
5. 更新 group descriptor 计数。
6. 更新 super block 计数。

释放反向清除 bitmap，并恢复计数。

metadata 更新当前偏同步，优先保证教学内核的一致性和可调试性，而不是高性能延迟写。

## truncate 和 fallocate

truncate 负责：

- 缩小时清零新 EOF 到块尾的内容并同步，防止未来扩展暴露旧数据。
- 释放超出新大小的 direct/indirect block。
- 更新 inode size 和 blocks。
- 截断 page cache。

扩展时：

- 若从块中间扩展，旧 EOF 到块尾要置零，保证空洞语义。
- 新增页仍按需分配。

fallocate 通过 inode operation 预分配指定范围所需的块。

## statfs

`ext2_statfs()` 汇总 group descriptor 中的 free block/inode 计数，填充 `struct statfs64`：

- `f_type = EXT2_SUPER_MAGIC`
- `f_bsize = BLOCK_SIZE`
- `f_blocks`
- `f_bfree/f_bavail`
- `f_files/f_ffree`
- `f_namelen = EXT2_NAME_LEN`

## 设计约束

- ext2 磁盘结构只在 `fs/ext2/` 内部使用。
- 文件数据和目录数据必须通过 inode page mapping 访问，不要绕过到 raw block cache。
- metadata 可以用 `page_cache_get_block()`，因为它以物理块号命名。
- 修改磁盘结构体字段必须保持 packed 布局和 ext2 格式一致。
- VFS 层不应知道 ext2 block group、bitmap、direct/indirect 细节。
