; 1.8 exception edge cases
; HT-30: a real-mode block copied to 08000h reaches CS through a far JMP, CALL, RETF or IRET, sets PE without a far
; jump and writes or reads a byte through CS
; HT-42: user code raises an exception whose ring-0 stack page is not present; #PF and #DF are task gates, or the
; kernel itself faults with ESP in that page
; HT-56: a block at 08000h loads DS, SS or GS with 0 in real mode, sets PE without a far jump and uses them, or returns
; to ring 3 with them, so the outer return's nulling shows

cpu 386
bits 32

%include "layout.inc"

extern cpu_longjmp

global rm_enter
global rm_block
global rm_block_end
global rm_variant
global rm_access
global rm_read_value
global rm_data
global ht42_enter
global ht42_pf_task
global ht42_df_task
global ht42_jump
global ht42_task_vector
global ht42_task_error
global ht42_task_cr2
global ht42_task_tr
global ht42_continued
global ht56_enter
global ht56_block
global ht56_block_end
global ht56_variant
global ht56_data
global ht56_value
global ht56_word
global ht56_sp
global ht56_selectors
global ht56_stage

RM_BASE             equ 0x8000
RM_SEGMENT          equ 0x0800
RM_STACK            equ 0x7FF0
HT42_USER_CS        equ 0x33
HT42_USER_SS        equ 0x3B
HT42_NP_DATA        equ 0x53
HT42_BAD_TSS        equ 0x5B
HT42_NP_STACK       equ 0x63
HT42_NP_TSS         equ 0x6B
HT42_DPL0_TSS       equ 0x73
HT42_RING0_STACK    equ 0x00007000
KIND_UNDEFINED      equ 6
KIND_TASK_NP        equ 8
KIND_TASK_GP        equ 9
KIND_KERNEL_PAGE    equ 10
KIND_IDT_PAGE       equ 11
KIND_GATE_NP        equ 12
HT42_USER_STACK     equ 0x0000D000
HT42_USER_NP        equ 0x00005000

%macro SET_PE 0
    mov eax, cr0
    or al, 1
    mov cr0, eax
%endmacro

%define HT56(label) (RM_BASE + label - ht56_block)

section .bss

alignb 4
rm_saved_esp:
    resd 1
ht42_kernel_esp:
    resd 1
ht42_jump:
    resd 1
ht42_task_vector:
    resd 1
ht42_task_error:
    resd 1
ht42_task_cr2:
    resd 1
ht42_task_tr:
    resd 1
ht42_continued:
    resd 1

section .text

; void rm_enter(void): runs the block at 08000h and returns in flat protected mode, unless a fault is caught
rm_enter:
    pushad
    mov [rm_saved_esp], esp
    jmp SEL_CODE16:(RM_BASE + rm_entry16 - rm_block)

rm_return:
    mov ax, SEL_DATA32
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, [rm_saved_esp]
    popad
    ret

; void ht42_enter(uint32_t kind, uint32_t page_directory): paging on, then IRET to CPL 3, or the CPL 0 page fault
ht42_enter:
    mov ebx, [esp + 4]
    mov eax, [esp + 8]
    mov cr3, eax
    mov eax, cr0
    or eax, 0x80000000
    mov cr0, eax
    jmp .paged
.paged:
    cmp ebx, KIND_KERNEL_PAGE
    je .kernel
    cmp ebx, KIND_IDT_PAGE
    je .idt
    cmp ebx, KIND_GATE_NP
    je .gate
    push dword HT42_USER_SS
    push dword HT42_USER_STACK
    push dword 0x00000002
    push dword HT42_USER_CS
    push dword ht42_user
    iretd
.kernel:
    mov [ht42_kernel_esp], esp
    mov esp, HT42_RING0_STACK
    mov eax, [HT42_USER_NP]
    mov esp, [ht42_kernel_esp]
    jmp .kernel_continued
.idt:
    mov eax, [HT42_USER_NP]
    jmp .kernel_continued
.gate:
    mov ax, 0xFFF8
    mov ds, ax
.kernel_continued:
    mov dword [ht42_continued], 1
    push dword 1
    push dword [ht42_jump]
    call cpu_longjmp

; CPL 3, EBX = kind: #DE, #TS, #NP, #SS, #GP, #PF, #UD, INT3, then #NP and #GP from a JMP to a TSS
; If the instruction doesn't fault, the code marks it through SS (DS is null at CPL 3) and raises #UD instead
ht42_user:
    cmp ebx, 0
    je .divide
    cmp ebx, 1
    je .task
    cmp ebx, 2
    je .not_present
    cmp ebx, 3
    je .stack
    cmp ebx, 4
    je .protection
    cmp ebx, 5
    je .page
    cmp ebx, KIND_UNDEFINED
    je .undefined
    cmp ebx, KIND_TASK_NP
    je .task_np
    cmp ebx, KIND_TASK_GP
    je .task_gp
    int3
    jmp .continued
.divide:
    xor ecx, ecx
    xor edx, edx
    mov eax, 1
    div ecx
    jmp .continued
.task:
    jmp HT42_BAD_TSS:0
    jmp .continued
.task_np:
    jmp HT42_NP_TSS:0
    jmp .continued
.task_gp:
    jmp HT42_DPL0_TSS:0
    jmp .continued
.not_present:
    mov ax, HT42_NP_DATA
    mov ds, ax
    jmp .continued
.stack:
    mov ax, HT42_NP_STACK
    mov ss, ax
    jmp .continued
.protection:
    cli
    jmp .continued
.page:
    mov eax, [ss:HT42_USER_NP]
    jmp .continued
.undefined:
    db 0x0F, 0x0B
.continued:
    mov dword [ss:ht42_continued], 1
    db 0x0F, 0x0B
.hang:
    jmp .hang

; void ht56_enter(void): runs the null-selector block at 08000h with GS holding the ring-3 data selector
ht56_enter:
    pushad
    mov [rm_saved_esp], esp
    mov ax, HT42_USER_SS
    mov gs, ax
    jmp SEL_CODE16:HT56(ht56_entry16)

; CPL 0 after the block's far jump, DS or GS still the real-mode 0: ES gets a null with RPL 3, FS a DPL 0 data
; selector, then IRET to ring 3
ht56_pm:
    mov ax, SEL_DATA32
    mov ss, ax
    mov esp, [cs:rm_saved_esp]
    mov ax, 3
    mov es, ax
    mov ax, SEL_DATA32
    mov fs, ax
    push dword HT42_USER_SS
    push dword HT42_USER_STACK
    push dword 0x00000002
    push dword HT42_USER_CS
    push dword ht56_user
    iretd

; CPL 3, BL = variant: the selectors as the return left them, then a read through DS (3) or GS (4), and #UD
ht56_user:
    mov [ss:HT56(ht56_selectors)], ds
    mov [ss:HT56(ht56_selectors) + 2], es
    mov [ss:HT56(ht56_selectors) + 4], fs
    mov [ss:HT56(ht56_selectors) + 6], gs
    mov byte [ss:HT56(ht56_stage)], 1
    cmp bl, 3
    jne .gs
    mov al, [ds:HT56(ht56_data)]
    jmp .read
.gs:
    mov al, [gs:HT56(ht56_data)]
.read:
    mov [ss:HT56(ht56_value)], al
    mov byte [ss:HT56(ht56_stage)], 2
    db 0x0F, 0x0B

; Task entries: the error code is on the task's stack
ht42_pf_task:
    mov dword [ht42_task_vector], 14
    jmp ht42_task_common
ht42_df_task:
    mov dword [ht42_task_vector], 8
ht42_task_common:
    pop dword [ht42_task_error]
    mov eax, cr2
    mov [ht42_task_cr2], eax
    str ax
    movzx eax, ax
    mov [ht42_task_tr], eax
    push dword 1
    push dword [ht42_jump]
    call cpu_longjmp

section .data

align 16
rm_block:

bits 16

rm_entry16:
    mov ax, SEL_DATA16
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov eax, cr0
    and al, 0xFE
    mov cr0, eax
    jmp RM_SEGMENT:(rm_real - rm_block)

rm_real:
    xor ax, ax
    mov ss, ax
    mov esp, RM_STACK
    mov ax, RM_SEGMENT
    mov ds, ax
    mov es, ax
    mov al, [rm_variant - rm_block]
    cmp al, 0
    je .jump
    cmp al, 1
    je .call
    cmp al, 2
    je .return
    pushf
    push word RM_SEGMENT
    push word (rm_target - rm_block)
    iret
.jump:
    jmp RM_SEGMENT:(rm_target - rm_block)
.call:
    call RM_SEGMENT:(rm_target - rm_block)
.return:
    push word RM_SEGMENT
    push word (rm_target - rm_block)
    retf

rm_target:
    mov eax, cr0
    or al, 1
    mov cr0, eax
    cmp byte [rm_access - rm_block], 0
    jne .read
    mov byte [cs:(rm_data - rm_block)], 0x5A
    jmp .done
.read:
    mov al, [cs:(rm_data - rm_block)]
    mov [rm_read_value - rm_block], al
.done:
    mov eax, cr0
    and al, 0xFE
    mov cr0, eax
    mov eax, cr0
    or al, 1
    mov cr0, eax
    jmp dword SEL_CODE32:rm_return

rm_variant:
    db 0
rm_access:
    db 0
rm_read_value:
    db 0
rm_data:
    db 0xC3

rm_block_end:

bits 16

align 16
ht56_block:

ht56_entry16:
    mov ax, SEL_DATA16
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov eax, cr0
    and al, 0xFE
    mov cr0, eax
    jmp RM_SEGMENT:(ht56_real - ht56_block)

; SS keeps SEL_DATA16's cache (base 0) unless the variant loads it, so a caught fault runs on a flat stack
ht56_real:
    mov esp, RM_STACK
    mov ax, RM_SEGMENT
    mov ds, ax
    mov es, ax
    movzx ebx, byte [ht56_variant - ht56_block]
    cmp bl, 1
    je .stack
    cmp bl, 2
    je .protected
    cmp bl, 3
    jae .ring3
    xor ax, ax
    mov ds, ax
    SET_PE
    mov byte [HT56(ht56_data)], 0xA5
    mov al, [HT56(ht56_data)]
    mov [es:ht56_value - ht56_block], al
    jmp .done
.stack:
    xor ax, ax
    mov ss, ax
    mov esp, RM_STACK
    SET_PE
    mov ax, 0x5AA5
    push ax
    xor ax, ax
    pop ax
    mov [ht56_word - ht56_block], ax
    mov [ht56_sp - ht56_block], sp
    jmp .done
.protected:
    SET_PE
    xor ax, ax
    mov ds, ax
    mov byte [HT56(ht56_data)], 0xA5
    jmp .done
.ring3:
    xor ax, ax
    cmp bl, 3
    jne .gs
    mov ds, ax
    jmp .enter
.gs:
    mov gs, ax
.enter:
    SET_PE
    jmp dword SEL_CODE32:ht56_pm
.done:
    jmp dword SEL_CODE32:rm_return

ht56_variant:
    db 0
ht56_data:
    db 0xC3
ht56_value:
    db 0
ht56_stage:
    db 0
ht56_word:
    dw 0
ht56_sp:
    dw 0
ht56_selectors:
    dw 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF

ht56_block_end:
