; Memory layout shared by the boot record, the shell and the tests (layout.h holds the same values for C)

IPL_BASE                    equ 0x000B0000
PAYLOAD_BASE                equ 0x00080000
PAYLOAD_LIMIT               equ 0x00030000
IPL_PAYLOAD_SECTORS_OFFSET  equ 0x40

STORE_BASE                  equ 0x00010000
PRISTINE_BASE               equ 0x00040000
RING0_STACK_TOP             equ 0x00072000
TEST_STACK_BASE             equ 0x00072000
TEST_STACK_TOP              equ 0x00078000
STACK_TOP                   equ 0x0007FFF0
SCRATCH_BASE                equ 0x00100000

SEL_CODE32                  equ 0x08
SEL_DATA32                  equ 0x10
SEL_CODE16                  equ 0x18
SEL_DATA16                  equ 0x20
SEL_TSS                     equ 0x28
