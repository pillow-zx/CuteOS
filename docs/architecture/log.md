# 内核日志与 syslog

printk 环形缓冲是内核唯一的有界日志存储；`syslog(116)` 是它的 Linux riscv64
ABI 适配层。`kernel/printk.c` 拥有环形缓冲、覆盖/游标策略、console 格式化、
ring 锁和等待通道；`syscall/sys_log.c` 只做 ABI 验证与委派。

## 环形缓冲

`PRINTK_LOG_BUF_SIZE = 4096` 的字节 ring，写满后覆盖最旧内容。每字节对应
一个全局单调 sequence，head 是最新字节。

- 写入在 ring 锁下追加并推进 head，随后唤醒 `read_wait` 等待通道。
- 两个归一化游标由同一锁保护，不向调用者暴露：
  - `read_seq`：动作 2 的全局 destructive 读取位置。
  - `clear_seq`：动作 3--5 的独立 snapshot-clear 位置。
- `read_lock`（mutex）串行化所有读取动作；`read_wait` 供阻塞读取者等待新日志。

## 读取动作

| 动作 | 语义 |
| --- | --- |
| 0/1 | NOP |
| 2 | 全局 destructive reader：从 `read_seq` 读 `len` 字节并推进；空日志时可被信号打断返回 `-EINTR` |
| 3 | 自最近一次 clear 后返回最后 `len` 字节 |
| 4 | 同 3，成功复制后执行同一 clear |
| 5 | 只移动 clear 标记，影响 3/4 的可见性，不影响 2/9 |
| 6--8 | console control 未实现，返回 `-ENOSYS` |
| 9 | 动作 2 的未读字节数 |
| 10 | 固定容量 4096 |

## 权限与错误

动作 3 和 10 无条件可读；其余有效动作要求 UID 0，作为 `CAP_SYSLOG` 的明确
临时替代。未知 action、动作 2--4 的 NULL buffer 或负 size 返回 `-EINVAL`；
输出用户地址不可访问返回 `-EFAULT`。

## 所有权边界

- 游标字段和它们的同步只属于 printk 模块；syscall 层不复制或直接读它们。
- console loglevel 策略未实现；`dmesg -r`/`-c` 由用户态组合 3/4/9/10 完成。
