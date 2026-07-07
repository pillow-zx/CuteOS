#ifndef _CUTEOS_DRIVERS_VIRTIO_BLK_H
#define _CUTEOS_DRIVERS_VIRTIO_BLK_H

/**
 * @file virtio_blk.h
 * @brief virtio-blk device-number contract and initialization API.
 */

#include <kernel/blkdev.h>

/**
 * @def VIRTIO_BLK_MAJOR
 * @brief Linux-compatible major number used for virtio block devices.
 */
#define VIRTIO_BLK_MAJOR 8u

/**
 * @def ROOT_DEV
 * @brief Root filesystem block device, currently virtio-blk disk 0.
 */
#define ROOT_DEV MKDEV(VIRTIO_BLK_MAJOR, 0)

/**
 * @brief Discover and register the QEMU virtio-blk root device.
 */
void virtio_blk_init(void);

#ifdef CONFIG_KERNEL_TEST
/**
 * @struct virtio_blk_test_stats
 * @brief Test-only counters exported by the polling virtio-blk driver.
 *
 * @par Fields
 * - @c read_reqs: Number of completed read requests.
 * - @c write_reqs: Number of completed write requests.
 * - @c max_write_nsec: Largest write size in sectors.
 * - @c last_write_nsec: Sector count of the most recent write.
 */
struct virtio_blk_test_stats {
	uint32_t read_reqs;
	uint32_t write_reqs;
	uint32_t max_write_nsec;
	uint32_t last_write_nsec;
};

void virtio_blk_test_reset_stats(void);
void virtio_blk_test_get_stats(struct virtio_blk_test_stats *stats);
#endif

#endif
