/* linker.ld - kernel is loaded at 0x1000 by the bootloader */
ENTRY(_start)

SECTIONS
{
    . = 0x1000;

    .text : {
        *(.text)
    }

    .rodata : {
        *(.rodata*)
    }

    .data : {
        *(.data)
    }

    .bss : {
        *(.bss)
        *(COMMON)
    }

    __kernel_end = .;
}
