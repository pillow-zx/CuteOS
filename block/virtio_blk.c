/*
 * block/virtio_blk.c - virtio-blk MMIO 驱动（轮询模式）
 *
 * 功能：
 *   实现 virtio-blk 设备（MMIO 传输方式）的驱动程序，用于 QEMU virt
 *   平台的虚拟块设备。采用轮询模式：写入描述符 → kick → 自旋等待
 *   used ring 响应。不使用中断。
 *
 * 主要函数：
 *   virtio_blk_init()              - 检测并初始化 virtio-blk MMIO 设备，
 *                   设置寄存器、协商特性、建立 virtqueue，注册块设备。
 *   virtio_blk_read(bdev, buf, sector, count)  - 从指定扇区读取数据。
 *                   构造请求描述符，kick 设备，轮询 used ring 等待完成。
 *   virtio_blk_write(bdev, buf, sector, count) - 向指定扇区写入数据。
 *                   流程同 read，请求类型为写。
 */
