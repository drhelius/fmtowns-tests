; Interrupt entry points, descriptor table loading and the jumps the shell uses to leave a test or a faulting step
; Every vector has a stub that pushes an error code (zero where the CPU pushes none) and its number, then
; cpu_common saves the registers and calls cpu_dispatch with the frame

cpu 386
bits 32

%include "layout.inc"

extern cpu_dispatch
extern cpu_double_fault
global cpu_stub_table
global cpu_double_fault_entry
global cpu_load_tables
global cpu_setjmp
global cpu_longjmp
global cpu_call_on_stack

section .text

%assign vector 0
%rep 256
stub_%[vector]:
%if vector == 8 || (vector >= 10 && vector <= 14) || vector == 17
    push dword vector
%else
    push dword 0
    push dword vector
%endif
    jmp cpu_common
%assign vector vector + 1
%endrep

cpu_common:
    pushad
    push ds
    push es
    push fs
    push gs
    mov ax, SEL_DATA32
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    cld
    mov eax, esp
    push eax
    call cpu_dispatch
    add esp, 4
    pop gs
    pop fs
    pop es
    pop ds
    popad
    add esp, 8
    iretd

; The double-fault task starts here, with the error code on its own stack
cpu_double_fault_entry:
    add esp, 4
    call cpu_double_fault
.halt:
    hlt
    jmp .halt

; void cpu_load_tables(const void* gdtr, const void* idtr)
cpu_load_tables:
    mov eax, [esp + 4]
    lgdt [eax]
    mov eax, [esp + 8]
    lidt [eax]
    jmp SEL_CODE32:.reload
.reload:
    mov ax, SEL_DATA32
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov ax, SEL_TSS
    ltr ax
    xor eax, eax
    lldt ax
    ret

; int cpu_setjmp(cpu_jmp_t* jump)
cpu_setjmp:
    mov eax, [esp + 4]
    mov [eax + 0], ebx
    mov [eax + 4], esi
    mov [eax + 8], edi
    mov [eax + 12], ebp
    lea ecx, [esp + 4]
    mov [eax + 16], ecx
    mov ecx, [esp]
    mov [eax + 20], ecx
    pushfd
    pop dword [eax + 24]
    xor eax, eax
    ret

; void cpu_longjmp(cpu_jmp_t* jump, int value)
; Reloads every data segment first, since the fault may have come with a real-mode or user segment loaded
cpu_longjmp:
    mov edx, [esp + 4]
    mov eax, [esp + 8]
    mov cx, SEL_DATA32
    mov ds, cx
    mov es, cx
    mov fs, cx
    mov gs, cx
    mov ss, cx
    mov ebx, [edx + 0]
    mov esi, [edx + 4]
    mov edi, [edx + 8]
    mov ebp, [edx + 12]
    mov esp, [edx + 16]
    push dword [edx + 24]
    popfd
    jmp [edx + 20]

; void cpu_call_on_stack(void* function, uint32_t first, uint32_t second, uint32_t stack_top)
cpu_call_on_stack:
    push ebp
    mov ebp, esp
    mov eax, [ebp + 8]
    mov ecx, [ebp + 12]
    mov edx, [ebp + 16]
    mov esp, [ebp + 20]
    push edx
    push ecx
    call eax
    mov esp, ebp
    pop ebp
    ret

section .rodata

align 4
cpu_stub_table:
%assign vector 0
%rep 256
    dd stub_%[vector]
%assign vector vector + 1
%endrep
