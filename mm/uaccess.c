/*
 * mm/uaccess.c - 用户空间内存访问
 *
 * 功能：
 *   提供内核与用户空间之间安全的数据拷贝函数。access_ok() 执行溢出检查：
 *   验证 a+size >= a（无溢出）且 a+size <= TASK_SIZE（在用户空间范围内）。
 *
 * 当前实现：
 *   copy_to_user / copy_from_user 目前直接使用 memcpy，未做异常处理。
 *   内核可直接访问整个地址空间，依赖 access_ok 边界检查防止越界。
 *
 * 后续计划：
 *   Stage 6 将添加异常处理机制，当用户地址无效时优雅地返回错误
 *   而非触发内核崩溃。
 *
 * 主要函数：
 *   access_ok(addr, size)        - 检查用户地址范围是否合法（溢出检查）
 *   copy_to_user(to, from, n)    - 从内核空间复制数据到用户空间（当前为 memcpy）
 *   copy_from_user(to, from, n)  - 从用户空间复制数据到内核空间（当前为 memcpy）
 */
