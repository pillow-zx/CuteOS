/*
 * arch/riscv/boot.S - 内核入口 _start
 *
 * 功能：
 *   RISC-V 64 位内核的早期启动入口，由 OpenSBI (M 模式) 跳转至此执行。
 *   在 MMU 关闭、物理地址直连的环境下完成最低限度的初始化，
 *   最终建立临时页表并开启 Sv39 MMU，跳转到内核虚拟地址空间。
 *
 * 6 步启动流程：
 *   1. Hart 检查与停泊：读取 tp (hartid)，非零 hart 立即 wfi 停泊，
 *      仅允许 hart 0（boot hart）继续执行，避免多核竞争。
 *   2. 设置 4KB 启动栈：sp = boot_stack_top，为后续 C 调用提供栈空间。
 *   3. 清零 BSS 段：遍历 __bss_start 到 _end，按 8 字节对齐清零，
 *      确保所有全局未初始化变量为 0。
 *   4. 建立 512 项临时 L2 root page table (tmp_root)：
 *      - L2[0]   → 1GB mega page 映射 MMIO 设备空间 (0x10000000 起)
 *      - L2[2]   → 1GB mega page 恒等映射 DRAM 低地址 (0x80000000 起)
 *      - L2[258] → 1GB mega page 高地址映射 DRAM (0xFFFFFFC000000000 起)
 *      共 3 项有效映射，其余清零。
 *   5. 写入 satp CSR 开启 Sv39 MMU：satp = (MODE=Sv39 << 60) | (tmp_root >> 12)，
 *      执行 sfence.vma 刷新 TLB，从此所有地址访问经过 MMU 翻译。
 *   6. 跳转至 kernel_main_virt：使用 jr 指令跳转到内核虚拟地址，
 *      进入 Supervisor 模式下的内核主初始化流程。
 *
 * 注意事项：
 *   - 临时页表仅使用 1GB mega page，不经过多级遍历
 *   - DRAM 临时映射为 R/W/X，MMIO 临时映射为 R/W（不可执行）
 *   - kernel_main_virt 位于高地址空间 (0xFFFFFFC0xxxxxxxx)，由链接脚本决定
 *   - 非零 hart 永久 wfi 停泊，后续由内核 S 态 SMP 启动代码唤醒
 */


#define PTE_V   (1 << 0)
#define PTE_R   (1 << 1)
#define PTE_W   (1 << 2)
#define PTE_X   (1 << 3)
#define PTE_G   (1 << 5)

#define PTE_MEGA_DRAM_FLAGS  (PTE_V | PTE_R | PTE_W | PTE_X | PTE_G)
#define PTE_MEGA_MMIO_FLAGS  (PTE_V | PTE_R | PTE_W | PTE_G)

        .section .text.entry
        .globl _start
_start:
        
        mv      s0, a0
        bnez    s0, .park

        .option push
        .option norelax
        la      gp, __global_pointer$
        .option pop

        lla     sp, boot_stack_top

        
        lla     t0, __bss_start
        lla     t1, _end
1:
        sd      zero, 0(t0)
        addi    t0, t0, 8
        bne     t0, t1, 1b

        
        lla     t0, tmp_root

        
        li      t1, (0x80000000 >> 2) | PTE_MEGA_DRAM_FLAGS
        sd      t1, (2*8)(t0)

        
        li      t2, 258*8
        add     t2, t0, t2
        sd      t1, 0(t2)

        
        li      t1, (0x00000000 >> 2) | PTE_MEGA_MMIO_FLAGS
        sd      t1, (0*8)(t0)

        
        lla     t0, tmp_root
        srli    t0, t0, 12              
        li      t1, (8 << 60)           
        or      t0, t0, t1
        csrw    satp, t0
        sfence.vma

        
        lla     t0, kernel_main
        li      t1, 0xFFFFFFC000000000  
        add     t0, t0, t1
        jr      t0

.park:
        wfi
        j       .park

        
        .section .bss
        .align  12
tmp_root:
        .space  4096                    
boot_stack:
        .space  4096                    
        .globl  boot_stack_top
boot_stack_top:
