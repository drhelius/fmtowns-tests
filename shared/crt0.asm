; Payload entry, linked first at PAYLOAD_BASE
; Clears BSS, keeps the boot device and calls main with interrupts off

cpu 386
bits 32

%include "layout.inc"

extern main
extern __bss_start
extern __bss_end
global _start
global g_boot_device

section .text.start

_start:
    mov esp, STACK_TOP
    cld
    mov edi, __bss_start
    mov ecx, __bss_end
    sub ecx, edi
    xor eax, eax
    rep stosb
    mov [g_boot_device], ebx
    call main

halt:
    hlt
    jmp halt

section .bss

g_boot_device:
    resd 1
