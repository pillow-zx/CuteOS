/*
 * block/buffer.c - Buffer Cache（缓冲区缓存）
 *
 * 功能：
 *   实现内核的 Buffer Cache，是块设备 I/O 的核心抽象层。每个 buffer_head
 *   代表一个磁盘块的缓存，包含 dev、blocknr、data、refcount、hash 字段。
 *   使用 128 桶哈希表快速查找。最多缓存 512 个 buffer_head，无淘汰机制。
 *
 * 数据结构：
 *   buffer_head {dev, blocknr, data, refcount, hash}
 *   hash_table[128]  - 128 桶哈希表
 *   b_data 通过 kmalloc(1024) 分配
 *
 * 读写策略：
 *   写穿透（write-through）：mark_buffer_dirty 后立即写磁盘。
 *
 * 主要函数：
 *   bread(dev, blocknr)  - 读取指定块。先查哈希表，命中则返回；
 *             未命中则分配 buffer_head 并从磁盘读取后加入哈希表。
 *   brelse(bh)           - 释放 buffer_head 引用（减少 refcount），
 *             缓冲区保留在哈希表中不驱逐。
 *   mark_buffer_dirty(bh) - 标记为脏并立即同步写磁盘（写穿透）。
 */
