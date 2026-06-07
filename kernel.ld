OUTPUT_ARCH(riscv)
ENTRY(_start)

BASE_ADDRESS = 0x80200000;
KERNEL_VBASE = 0xFFFFFFC000000000;

PHDRS
{
  text   PT_LOAD FLAGS(5);   /* R|X */
  rodata PT_LOAD FLAGS(4);   /* R */
  data   PT_LOAD FLAGS(6);   /* R|W */
  bss    PT_LOAD FLAGS(6);   /* R|W */
}

SECTIONS
{
    . = KERNEL_VBASE + BASE_ADDRESS;

    .text : AT(ADDR(.text) - KERNEL_VBASE) {
        *(.text.entry)
        . = ALIGN(4096);
        *(.text .text.*)
        . = ALIGN(4096);
    } :text

    .rodata : AT(ADDR(.rodata) - KERNEL_VBASE) {
        *(.rodata .rodata.*)
        . = ALIGN(4096);
    } :rodata

    .data : AT(ADDR(.data) - KERNEL_VBASE) {
        *(.data .data.*)
        . = ALIGN(4096);
    } :data

    .bss : AT(ADDR(.bss) - KERNEL_VBASE) {
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(4096);
        __bss_end = .;
    } :bss

    . = ALIGN(4096);
    _end = .;
}
