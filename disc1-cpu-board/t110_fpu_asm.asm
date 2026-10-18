; 1.10 FPU results: one transcendental or remainder instruction on two 80-bit operands under a control word
; void fpu_apply(int op, const void* first, const void* second, uint32_t control, void* out)
; out: ST0 (10 bytes), ST1 (10 bytes), status word before the stores (2 bytes)
; void fpu_operand(int kind, int k, void* out): builds an operand on the FPU

cpu 386
bits 32

global fpu_apply
global fpu_operand

section .bss

alignb 4
control_word:
    resd 1
scratch:
    resb 16

section .text

fpu_apply:
    push ebx
    push esi
    push edi
    mov ecx, [esp + 16]
    mov esi, [esp + 20]
    mov ebx, [esp + 24]
    mov eax, [esp + 28]
    mov [control_word], eax
    mov edi, [esp + 32]
    fninit
    fldcw [control_word]
    fld tword [ebx]
    fld tword [esi]
    cmp ecx, 0
    je .sin
    cmp ecx, 1
    je .cos
    cmp ecx, 2
    je .sincos
    cmp ecx, 3
    je .ptan
    cmp ecx, 4
    je .patan
    cmp ecx, 5
    je .f2xm1
    cmp ecx, 6
    je .yl2x
    cmp ecx, 7
    je .yl2xp1
    cmp ecx, 8
    je .scale
    cmp ecx, 9
    je .prem
    fprem1
    jmp .store
.sin:
    fsin
    jmp .store
.cos:
    fcos
    jmp .store
.sincos:
    fsincos
    jmp .store
.ptan:
    fptan
    jmp .store
.patan:
    fpatan
    jmp .store
.f2xm1:
    f2xm1
    jmp .store
.yl2x:
    fyl2x
    jmp .store
.yl2xp1:
    fyl2xp1
    jmp .store
.scale:
    fscale
    jmp .store
.prem:
    fprem
.store:
    fnstsw ax
    mov [edi + 20], ax
    fstp tword [edi]
    fstp tword [edi + 10]
    fninit
    pop edi
    pop esi
    pop ebx
    ret

; kind 0: k * pi / 4, kind 1: 2^k, kind 2: k / 16, all rounded to the nearest 64-bit mantissa
fpu_operand:
    mov ecx, [esp + 4]
    mov eax, [esp + 8]
    mov edx, [esp + 12]
    fninit
    mov [scratch], eax
    cmp ecx, 1
    je .power
    cmp ecx, 2
    je .fraction
    fldpi
    fild dword [scratch]
    fmulp st1, st0
    mov dword [scratch], 4
    fild dword [scratch]
    fdivp st1, st0
    jmp .done
.power:
    fild dword [scratch]
    fld1
    fscale
    fstp st1
    jmp .done
.fraction:
    fild dword [scratch]
    mov dword [scratch], 16
    fild dword [scratch]
    fdivp st1, st0
.done:
    fstp tword [edx]
    fninit
    ret
