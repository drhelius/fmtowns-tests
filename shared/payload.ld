/* Payload linked at PAYLOAD_BASE (layout.inc), entry first */
/* .bss.shell holds the shell's state, which survives the reset of the tests' .data and .bss before each run */

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
        . = ALIGN(4);
        __data_start = .;
        *(.data*)
        . = ALIGN(4);
        __data_end = .;
    }

    .shell (NOLOAD) :
    {
        . = ALIGN(4);
        __shell_start = .;
        *(.bss.shell*)
        . = ALIGN(4);
        __shell_end = .;
    }

    .bss (NOLOAD) :
    {
        __bss_start = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        __bss_end = .;
    }

    ASSERT(__bss_end <= 0x000B0000, "the payload and its BSS must end below the boot record")
    ASSERT(__data_end - __data_start <= 0x00030000, "the data sections must fit their pristine copy")

    /DISCARD/ :
    {
        *(.comment)
        *(.eh_frame*)
        *(.note*)
    }
}
