/*
 * user/user.ld - 用户程序链接脚本
 *
 * 基址 0x10000，与 exec loader 的低地址用户映射一致。
 * .text.entry 放在最前，确保 _start 位于第一个可执行 LOAD。
 *
 * 用户 ELF 由内核按 PT_LOAD 的 p_flags 建立页权限。这里显式拆分
 * RX / R / RW 段，并让每个段页对齐，避免 ld 把 text/data 合并成
 * 一个 RWE LOAD。
 */

OUTPUT_ARCH(riscv)
ENTRY(_start)

PAGE_SIZE = 0x1000;

PHDRS
{
    text   PT_LOAD FLAGS(5); /* PF_R | PF_X */
    rodata PT_LOAD FLAGS(4); /* PF_R */
    data   PT_LOAD FLAGS(6); /* PF_R | PF_W */
    stack  PT_GNU_STACK FLAGS(6);
}

SECTIONS
{
    . = 0x10000;

    .text : {
        *(.text.entry)
        *(.text .text.*)
    } :text

    . = ALIGN(PAGE_SIZE);

    .rodata : {
        *(.srodata .srodata.*)
        *(.rodata .rodata.*)
        *(.eh_frame .eh_frame.*)
        *(.eh_frame_hdr)
    } :rodata

    . = ALIGN(PAGE_SIZE);

    .data : {
        PROVIDE(__global_pointer$ = . + 0x800);
        *(.got .got.*)
        *(.got.plt)
        *(.preinit_array .preinit_array.*)
        *(.init_array .init_array.*)
        *(.fini_array .fini_array.*)
        *(.data .data.*)
        *(.sdata .sdata.*)
    } :data

    .bss : {
        __bss_start = .;
        *(.sbss .sbss.*)
        *(.bss .bss.*)
        *(COMMON)
        __bss_end = .;
    } :data

    . = ALIGN(8);

    /DISCARD/ : {
        *(.comment)
        *(.note.*)
    }
}

ASSERT((ADDR(.text) % PAGE_SIZE) == 0, "user .text must be page aligned")
ASSERT((ADDR(.rodata) % PAGE_SIZE) == 0, "user .rodata must be page aligned")
ASSERT((ADDR(.data) % PAGE_SIZE) == 0, "user .data must be page aligned")
