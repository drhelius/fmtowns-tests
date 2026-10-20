; 2.3 VSYNC interrupt and IN sampling
; uint32_t irq_in_loop_N(void): Flashback's IN AL,DX / TEST AL,1 / JZ loop with INC ECX, the loop N bytes past a
; 16-byte boundary; returns the iterations
; void irq_flashback_loop(void): the same loop without the count
; void irq_port_reads(uint32_t port, uint32_t passes), irq_port_reads16: 16 unrolled IN AL,DX (IN AX,DX) a pass
; void irq_port_writes(uint32_t port, uint32_t value, uint32_t passes): 16 unrolled OUT DX,AL a pass
; void irq_segment_loads(uint32_t passes): 16 unrolled MOV DS,AX a pass, reloading DS
; void irq_segment_pops(uint32_t passes): 16 PUSH DS then 16 POP DS a pass
; void irq_align_S_N(uint32_t count): register-only loops of 3, 6 and 10 bytes (S = 0, 1, 2), N bytes past a 16-byte
; boundary, run count times
; void irq_soft_ints(uint32_t count): INT 30h, through the shell's dispatcher
; void irq_bare_ints(uint32_t count): INT 31h, to a gate that only returns
; irq_bare_vsync: IRQ11 straight from its gate, storing 0026h on entry, clearing the cause and ending both PICs

cpu 386
bits 32

global irq_in_loops
global irq_flashback_loop
global irq_port_reads
global irq_port_reads16
global irq_port_writes
global irq_segment_loads
global irq_segment_pops
global irq_align_loops
global irq_soft_ints
global irq_bare_ints
global irq_bare_iret
global irq_bare_vsync
global irq_bare_time
global irq_bare_count

section .text

%macro IN_LOOP 1
irq_in_loop_%1:
    xor ecx, ecx
    mov edx, 0xFDA0
    jmp .loop
align 16
    times %1 nop
.loop:
    in al, dx
    inc ecx
    test al, 1
    jz .loop
    mov eax, ecx
    ret
%endmacro

IN_LOOP 0
IN_LOOP 1
IN_LOOP 2
IN_LOOP 3
IN_LOOP 4
IN_LOOP 5
IN_LOOP 6
IN_LOOP 7

irq_flashback_loop:
    mov edx, 0xFDA0
    jmp .loop
align 16
.loop:
    in al, dx
    test al, 1
    jz .loop
    ret

irq_port_reads:
    mov edx, [esp + 4]
    mov ecx, [esp + 8]
.loop:
    times 16 in al, dx
    dec ecx
    jnz .loop
    ret

irq_port_reads16:
    mov edx, [esp + 4]
    mov ecx, [esp + 8]
.loop:
    times 16 in ax, dx
    dec ecx
    jnz .loop
    ret

irq_port_writes:
    mov edx, [esp + 4]
    mov eax, [esp + 8]
    mov ecx, [esp + 12]
.loop:
    times 16 out dx, al
    dec ecx
    jnz .loop
    ret

irq_segment_loads:
    mov ecx, [esp + 4]
    mov ax, ds
.loop:
    times 16 mov ds, ax
    dec ecx
    jnz .loop
    ret

irq_segment_pops:
    mov ecx, [esp + 4]
.loop:
    times 16 push ds
    times 16 pop ds
    dec ecx
    jnz .loop
    ret

%macro ALIGN_LOOP 2
irq_align_%1_%2:
    push ebx
    push esi
    mov ecx, [esp + 12]
    xor eax, eax
    xor ebx, ebx
    xor edx, edx
    jmp .loop
align 16
    times %2 nop
.loop:
%if %1 >= 1
    inc eax
    add ebx, eax
%endif
%if %1 >= 2
    xor edx, ebx
    mov esi, edx
%endif
    dec ecx
    jnz .loop
    pop esi
    pop ebx
    ret
%endmacro

%assign shape 0
%rep 3
%assign offset 0
%rep 8
ALIGN_LOOP shape, offset
%assign offset offset + 1
%endrep
%assign shape shape + 1
%endrep

irq_soft_ints:
    mov ecx, [esp + 4]
.loop:
    int 0x30
    dec ecx
    jnz .loop
    ret

irq_bare_ints:
    mov ecx, [esp + 4]
.loop:
    int 0x31
    dec ecx
    jnz .loop
    ret

irq_bare_iret:
    iretd

irq_bare_vsync:
    push eax
    push edx
    in ax, 0x26
    mov [irq_bare_time], eax
    mov dx, 0x05CA
    out dx, al
    mov al, 0x20
    out 0x10, al
    out 0x00, al
    inc dword [irq_bare_count]
    pop edx
    pop eax
    iretd

section .data

align 4
irq_bare_time:
    dd 0
irq_bare_count:
    dd 0

section .rodata

align 4
irq_align_loops:
%assign shape 0
%rep 3
    dd irq_align_%[shape]_0, irq_align_%[shape]_1, irq_align_%[shape]_2, irq_align_%[shape]_3
    dd irq_align_%[shape]_4, irq_align_%[shape]_5, irq_align_%[shape]_6, irq_align_%[shape]_7
%assign shape shape + 1
%endrep

irq_in_loops:
    dd irq_in_loop_0, irq_in_loop_1, irq_in_loop_2, irq_in_loop_3
    dd irq_in_loop_4, irq_in_loop_5, irq_in_loop_6, irq_in_loop_7
