/*
 * arch/riscv/mm/user_map.c - QEMU virt 用户页表平台映射
 *
 * 用户进程运行在自己的页表上，trap 进入内核后仍需要低地址 MMIO
 * 映射，供轮询 UART 和 virtio-blk 访问。映射权限不包含 PTE_U。
 */

#include <kernel/printk.h>
#include <kernel/user_map.h>
#include <asm/page.h>
#include <asm/pte.h>
#include <asm/user_map.h>
#include <drivers/uart.h>
#include <drivers/virtio.h>

#define RISCV_MMIO_USER_START UART_BASE
#define RISCV_MMIO_USER_END   (VIRTIO_MMIO_BASE + PAGE_SIZE)

static int riscv_user_mmio_map(pte_t *root)
{
	arch_map_page(root, UART_BASE, UART_BASE, PTE_KERN_RW);
	arch_map_page(root, VIRTIO_MMIO_BASE, VIRTIO_MMIO_BASE, PTE_KERN_RW);
	return 0;
}

void arch_user_map_init(void)
{
	int ret;

	ret = user_map_register_reserved("riscv_mmio", RISCV_MMIO_USER_START,
					 RISCV_MMIO_USER_END,
					 riscv_user_mmio_map);
	BUG_ON(ret < 0);
}
