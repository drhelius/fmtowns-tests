/* Payload linked at PAYLOAD_BASE (layout.inc), entry first */

ENTRY(_start)

SECTIONS
{
    . = 0x00080000;

    .text :
    {
        *(.text.start)
        *(.text*)
    }

    .rodata :
    {
        *(.rodata*)
    }

    .data :
    {
        *(.data*)
    }

    .bss :
    {
        __bss_start = .;
        *(.bss*)
        *(COMMON)
        __bss_end = .;
    }

    /DISCARD/ :
    {
        *(.comment)
        *(.eh_frame*)
        *(.note*)
    }
}
