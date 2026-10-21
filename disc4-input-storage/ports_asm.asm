; 4.8 Port timing
; void ports_reads(uint32_t port, uint32_t passes): 16 unrolled IN AL,DX a pass
; void ports_writes(uint32_t port, uint32_t value, uint32_t passes): 16 unrolled OUT DX,AL a pass
; void ports_empty(uint32_t passes): the same loop with no port access

cpu 386
bits 32

global ports_reads
global ports_writes
global ports_empty

section .text

ports_reads:
    mov edx, [esp + 4]
    mov ecx, [esp + 8]
.loop:
    times 16 in al, dx
    dec ecx
    jnz .loop
    ret

ports_writes:
    mov edx, [esp + 4]
    mov eax, [esp + 8]
    mov ecx, [esp + 12]
.loop:
    times 16 out dx, al
    dec ecx
    jnz .loop
    ret

ports_empty:
    mov ecx, [esp + 4]
.loop:
    dec ecx
    jnz .loop
    ret
