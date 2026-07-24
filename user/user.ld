/*
 * user/user.ld - 用户程序链接脚本
 */

OUTPUT_ARCH(riscv)
ENTRY(_start)

PAGE_SIZE = 0x1000;

PHDRS
{
    text   PT_LOAD FILEHDR PHDRS FLAGS(5);
    rodata PT_LOAD FLAGS(4);
    data   PT_LOAD FLAGS(6);
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
