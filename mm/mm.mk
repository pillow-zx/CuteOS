# mm/mm.mk — 体系无关的内存管理

MM_OBJS = \
	mm/buddy.o          \
	mm/slab.o           \
	mm/vmalloc.o        \
	mm/mmap.o           \
	mm/page_fault.o     \
	mm/uaccess.o
