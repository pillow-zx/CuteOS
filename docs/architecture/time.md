# 时间架构

## 时钟来源

RISC-V `time` CSR 是 cuteOS 的原始单调时钟。`arch_timer_now()` 返回 QEMU
`virt` 的 10 MHz mtime tick；内核 timer、调度 tick、相对 sleep 和 timeout
只以这个时钟计算期限。

`CLOCK_MONOTONIC` 与 `CLOCK_BOOTTIME` 直接转换 mtime。当前平台没有 suspend
模型，因此两者相同；它们不会因设置 wall clock 而跳变。

## CLOCK_REALTIME

`kernel/time.c` 拥有全局 `CLOCK_REALTIME` offset。读取 realtime 时，内核在
mtime 转换结果上加上这个 offset；写入时用请求值减去当前 mtime 重新计算它。

- 只有 UID 0 可通过 `clock_settime(CLOCK_REALTIME)` 写入。
- `tv_sec >= 0`、`0 <= tv_nsec < 1e9` 是必要条件。
- 请求值不能小于当前 `CLOCK_MONOTONIC`，否则返回 `-EINVAL`。
- offset 仅存在于内存中，启动时为零，重启后丢失。系统没有 RTC、NTP、频率调整
  或持久化策略。

`clock_gettime(CLOCK_REALTIME)`、`gettimeofday` 和 VFS 自动设置的 inode
atime/mtime/ctime 都经由这个接口读取时间。显式 `utimensat` 时间戳仍按调用者
提供的值写入。

## Timer 边界

相对等待和 interval timer 始终以 mtime 计时，因而不受 wall-clock 写入影响。
cuteOS 尚未实现 `clock_settime` 后重排已注册 wall-clock absolute deadline 的
机制。因此 `clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME)` 与
`timer_settime` 的 `CLOCK_REALTIME + TIMER_ABSTIME` 组合返回 `-EINVAL`；
`FUTEX_CLOCK_REALTIME` 返回 `-ENOSYS`。它们只能在具有 wake/recompute 和
timer 重排契约后启用。

`hwclock` 与 `rtcwake` 继续禁用，直到存在 RTC 设备及匹配 ioctl。
