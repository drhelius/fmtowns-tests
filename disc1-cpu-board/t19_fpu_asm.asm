; 1.9 coprocessor and CPU identification stubs: each takes a buffer in its first argument
; The 486 instructions only run on a 486 or inside a catch that takes their #UD

cpu 486
bits 32

global fpu_fninit
global fpu_fnstsw_m16
global fpu_fnstcw_m16
global fpu_fnstsw_ax
global fpu_fadd_fstp64
global fpu_fstp32
global fpu_fstp64
global fpu_fstp80
global fpu_fnsave
global fpu_wait
global fpu_fld1
global fpu_fadd_m64
global fpu_fstp_m64
global fpu_time
global fpu_divide_zero
global fpu_fstp80_at
global cpu_cmpxchg_b0
global cpu_cmpxchg_b1
global cpu_cmpxchg_a6
global cpu_cmpxchg_a7
global cpu_cpuid
global cpu_test_registers

PIT0_COUNTER0       equ 0x0040
PIT0_CONTROL        equ 0x0046

section .text

fpu_fninit:
    fninit
    ret

fpu_fnstsw_m16:
    mov eax, [esp + 4]
    fnstsw [eax]
    ret

fpu_fnstcw_m16:
    mov eax, [esp + 4]
    fnstcw [eax]
    ret

fpu_fnstsw_ax:
    mov ecx, [esp + 4]
    mov ax, 0x5A5A
    fnstsw ax
    mov [ecx], ax
    ret

fpu_fadd_fstp64:
    mov eax, [esp + 4]
    fld1
    fadd st0, st0
    fstp qword [eax]
    ret

fpu_fstp32:
    mov eax, [esp + 4]
    fld1
    fstp dword [eax]
    ret

fpu_fstp64:
    mov eax, [esp + 4]
    fld1
    fstp qword [eax]
    ret

fpu_fstp80:
    mov eax, [esp + 4]
    fld1
    fstp tword [eax]
    ret

fpu_fnsave:
    mov eax, [esp + 4]
    fnsave [eax]
    ret

fpu_wait:
    wait
    ret

fpu_fld1:
    fld1
    ret

fpu_fadd_m64:
    mov eax, [esp + 4]
    fadd qword [eax]
    ret

fpu_fstp_m64:
    mov eax, [esp + 4]
    fstp qword [eax]
    ret

; 1 / 0 with the zero-divide exception unmasked, leaving the error pending for the next WAIT
fpu_divide_zero:
    mov eax, [esp + 4]
    fninit
    fldcw [eax]
    fld1
    fldz
    fdivp st1, st0
    ret

; FSTP m80 at ES:EDI = buffer offset in the second argument, with ES limited by the caller
fpu_fstp80_at:
    push edi
    mov edi, [esp + 8]
    fld1
    fstp tword [es:edi]
    pop edi
    ret

; uint32_t fpu_time(int kind): PIT ticks for 64 FNINIT, 64 FNSTSW AX or 64 NOP
fpu_time:
    push ebx
    mov ecx, [esp + 8]
    mov al, 0
    out PIT0_CONTROL, al
    in al, PIT0_COUNTER0
    mov bl, al
    in al, PIT0_COUNTER0
    mov bh, al
    cmp ecx, 0
    je .fninit
    cmp ecx, 1
    je .fnstsw
%rep 64
    nop
%endrep
    jmp .done
.fninit:
%rep 64
    fninit
%endrep
    jmp .done
.fnstsw:
%rep 64
    fnstsw ax
%endrep
.done:
    mov al, 0
    out PIT0_CONTROL, al
    in al, PIT0_COUNTER0
    mov ah, al
    in al, PIT0_COUNTER0
    xchg al, ah
    sub bx, ax
    movzx eax, bx
    pop ebx
    ret

; CMPXCHG r/m8,r8 and r/m32,r32 at 0F B0/B1, and the early B-step encodings at 0F A6/A7, register forms
cpu_cmpxchg_b0:
    push ebx
    mov al, 1
    mov bl, 1
    mov cl, 2
    db 0x0F, 0xB0, 0xCB
    pop ebx
    ret

cpu_cmpxchg_b1:
    push ebx
    mov eax, 1
    mov ebx, 1
    mov ecx, 2
    db 0x0F, 0xB1, 0xCB
    pop ebx
    ret

cpu_cmpxchg_a6:
    push ebx
    mov al, 1
    mov bl, 1
    mov cl, 2
    db 0x0F, 0xA6, 0xCB
    pop ebx
    ret

cpu_cmpxchg_a7:
    push ebx
    mov eax, 1
    mov ebx, 1
    mov ecx, 2
    db 0x0F, 0xA7, 0xCB
    pop ebx
    ret

; void cpu_cpuid(uint32_t leaf, uint32_t* out): EAX, EBX, ECX, EDX
cpu_cpuid:
    push ebx
    push edi
    mov eax, [esp + 12]
    mov edi, [esp + 16]
    db 0x0F, 0xA2
    mov [edi], eax
    mov [edi + 4], ebx
    mov [edi + 8], ecx
    mov [edi + 12], edx
    pop edi
    pop ebx
    ret

; void cpu_test_registers(uint32_t* buffer): TR3, TR4 and TR5 written with buffer[0..2] and read into buffer[3..5],
; then a TR6/TR7 TLB write of buffer[6] (TR7) and buffer[7] (TR6) and a lookup of buffer[8] read into buffer[9..10]
cpu_test_registers:
    mov ecx, [esp + 4]
    mov eax, [ecx]
    db 0x0F, 0x26, 0xD8
    mov eax, [ecx + 4]
    db 0x0F, 0x26, 0xE0
    mov eax, [ecx + 8]
    db 0x0F, 0x26, 0xE8
    db 0x0F, 0x24, 0xD8
    mov [ecx + 12], eax
    db 0x0F, 0x24, 0xE0
    mov [ecx + 16], eax
    db 0x0F, 0x24, 0xE8
    mov [ecx + 20], eax
    mov eax, [ecx + 24]
    db 0x0F, 0x26, 0xF8
    mov eax, [ecx + 28]
    db 0x0F, 0x26, 0xF0
    mov eax, [ecx + 32]
    db 0x0F, 0x26, 0xF0
    db 0x0F, 0x24, 0xF8
    mov [ecx + 36], eax
    db 0x0F, 0x24, 0xF0
    mov [ecx + 40], eax
    ret
