# block/block.mk — 块设备层

BLOCK_OBJS = \
	block/blkdev.o          \
	block/page_cache_alias.o \
	block/page_cache.o      \
	block/page_cache_dirty.o \
	block/page_cache_writeback.o \
	block/virtio_blk.o
