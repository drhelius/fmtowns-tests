; Memory layout shared by the boot record and the payload

IPL_BASE                    equ 0x000B0000
PAYLOAD_BASE                equ 0x00080000
PAYLOAD_LIMIT               equ 0x00030000
STACK_TOP                   equ 0x0007FFF0
IPL_PAYLOAD_SECTORS_OFFSET  equ 0x40
