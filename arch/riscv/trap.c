/*
 * arch/riscv/trap.c - Trap 分发（C 层）
 *
 * 在汇编层保存完 trap_frame 后，由 trap_handler() 统一接住异常和中断。
 * 根据中断/异常类型分发到对应的处理函数。
 *
 * 当前分发：
 *   - Supervisor Timer Interrupt (scause = 0x8000000000000005)
 *     → handle_timer_irq(): 更新 jiffies, 设置下一次时钟中断
 *   - 其他中断/异常 → panic（开发期）
 */

#include <asm/csr.h>
#include <asm/trap.h>
#include <asm/sbi.h>
#include <kernel/printk.h>
#include <kernel/types.h>
#include <kernel/sched.h>
#include <kernel/task.h>
#include <kernel/timer.h>

static const char *trap_origin(const struct trap_frame *tf)
{
        return (tf->sstatus & SSTATUS_SPP) ? "kernel" : "user";
}

/*
 * handle_timer_irq() - 时钟中断处理
 *
 * 每次时钟中断时调用：
 *   1. 递增全局 jiffies 计数器
 *   2. 通过 set_mtimecmp 设置下一次时钟中断
 */
static void handle_timer_irq(void)
{
        jiffies++;
        set_mtimecmp(get_mtime() + CLOCKS_PER_TICK);

        /* 非 idle 的 RUNNING 进程标记需要调度 */
        if (current && current != &idle_task &&
            current->state == TASK_RUNNING)
                current->need_resched = 1;
}

/*
 * trap_handler() - 统一 trap 处理入口
 *
 * 由 entry.S 中的 __alltraps 调用，传入 trap_frame 指针。
 * 根据 scause 判断 trap 类型并分发。
 */
void trap_handler(struct trap_frame *tf)
{
        uint64_t scause = tf->scause;
        bool is_interrupt = (scause & SCAUSE_IRQ_FLAG) != 0;
        uint64_t code = scause & ~SCAUSE_IRQ_FLAG;

        if (is_interrupt) {
                switch (code) {
                case IRQ_S_TIMER:
                        handle_timer_irq();
                        if (current && current->need_resched) {
                                current->need_resched = 0;
                                schedule();
                        }
                        return;
                default:
                        panic("unhandled interrupt: origin=%s scause=0x%lx "
                              "code=%lu "
                              "sepc=%p stval=%p",
                              trap_origin(tf), (size_t)scause,
                              (size_t)code, (void *)tf->sepc,
                              (void *)tf->stval);
                }
        } else {
                panic("unhandled exception: origin=%s scause=0x%lx code=%lu "
                      "sepc=%p stval=%p",
                      trap_origin(tf), (size_t)scause,
                      (size_t)code, (void *)tf->sepc, (void *)tf->stval);
        }
}
