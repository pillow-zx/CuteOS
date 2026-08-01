# 独立记录 IRQ Nesting 与 Preempt Count

cuteOS 将 IRQ context nesting 与 `preempt_count` 作为两个独立的 CPU-local
状态维护：前者只表示当前 hard IRQ handler 深度，后者只表示显式禁抢占深度。
timer handler 结束后先退出 IRQ context，再执行用户态返回路径的调度；这样为未来
内核抢占保留清晰契约，避免用一个计数器同时表达两个不同条件。

调度入口显式拒绝 IRQ context，等待入口也拒绝 hard IRQ 和禁抢占 context；这些
诊断使用独立的只读上下文 guard，不把 IRQ nesting 编码进 `preempt_count`。测试通过
guard 验证非法路径，避免触发不可恢复的 kernel panic。
