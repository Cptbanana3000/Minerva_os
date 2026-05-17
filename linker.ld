/* linker.ld - kernel is loaded at 0x8000 by the bootloader */
ENTRY(_start)

SECTIONS
{
    . = 0x8000;

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
        __bss_start = .;
        *(.bss)
        *(COMMON)
        __bss_end = .;
    }

    __kernel_end = .;
}
