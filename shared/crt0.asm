; Payload entry, linked first at PAYLOAD_BASE
; Keeps a pristine copy of .data for the aseptic runs, clears the shell's state and BSS and calls main

cpu 386
bits 32

%include "layout.inc"

extern main
extern __data_start
extern __data_end
extern __shell_start
extern __bss_end
global _start
global g_boot_device
global g_boot_cr0
global g_boot_eflags

section .text.start

_start:
    mov esp, STACK_TOP
    cld
    push ebx
    push esi
    push edi

    mov esi, __data_start
    mov edi, PRISTINE_BASE
    mov ecx, __data_end
    sub ecx, esi
    rep movsb

    mov edi, __shell_start
    mov ecx, __bss_end
    sub ecx, edi
    xor eax, eax
    rep stosb

    pop dword [g_boot_eflags]
    pop dword [g_boot_cr0]
    pop dword [g_boot_device]
    call main

halt:
    hlt
    jmp halt

section .bss.shell nobits alloc noexec write align=4

g_boot_device:
    resd 1
g_boot_cr0:
    resd 1
g_boot_eflags:
    resd 1
