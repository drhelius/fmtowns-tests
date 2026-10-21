; 4.2 Mouse strobe timing, on the 0026h microsecond counter
; uint32_t mouse_packet(void* entry, const uint32_t* waits, uint32_t since): one packet. waits holds T4, T1, T2 and
; T3, then the time of the last nibble's late read. Edge i comes waits[i] counts after the one before (after since
; for the first), with COM low on even edges; the nibble is read 90 counts after the first edge and 45 after the
; others (early), and again 4 counts before the next edge (late). entry gets early[4], late[4] and the measured gaps
; as uint16_t[4]; returns the count of the last edge
; uint32_t mouse_packet_pit(void* entry, const uint32_t* waits, uint32_t since): the same on PIT counter 0, which
; counts down at 307.2 kHz; waits, the read points and the gaps are in its counts

cpu 386
bits 32

global mouse_packet
global mouse_packet_pit

section .text

mouse_packet:
    push ebx
    push esi
    push edi
    push ebp
    mov edi, [esp + 20]
    mov esi, [esp + 24]
    mov ebp, [esp + 28]
    xor ecx, ecx
    mov edx, 0x0026
.edge:
    mov ebx, [esi + ecx * 4]
.wait_edge:
    in ax, dx
    sub ax, bp
    cmp ax, bx
    jb .wait_edge
    mov al, 0x0F
    test cl, 1
    jz .write
    mov al, 0x2F
.write:
    mov dx, 0x04D6
    out dx, al
    mov dx, 0x0026
    in ax, dx
    mov bx, ax
    sub bx, bp
    mov [edi + 8 + ecx * 2], bx
    mov bp, ax
    mov ebx, 45
    test ecx, ecx
    jnz .early
    mov ebx, 90
.early:
    in ax, dx
    sub ax, bp
    cmp ax, bx
    jb .early
    mov dx, 0x04D2
    in al, dx
    mov [edi + ecx], al
    mov dx, 0x0026
    mov ebx, [esi + ecx * 4 + 4]
    sub ebx, 4
.late:
    in ax, dx
    sub ax, bp
    cmp ax, bx
    jb .late
    mov dx, 0x04D2
    in al, dx
    mov [edi + 4 + ecx], al
    mov dx, 0x0026
    inc ecx
    cmp ecx, 4
    jb .edge
    movzx eax, bp
    pop ebp
    pop edi
    pop esi
    pop ebx
    ret

; ax = PIT counter 0, latched
%macro READ_PIT 0
    mov al, 0x00
    out 0x46, al
    in al, 0x40
    mov ah, al
    in al, 0x40
    xchg al, ah
%endmacro

; ax = counts since bp on the down counter
%macro SINCE_PIT 0
    READ_PIT
    neg ax
    add ax, bp
%endmacro

mouse_packet_pit:
    push ebx
    push esi
    push edi
    push ebp
    mov edi, [esp + 20]
    mov esi, [esp + 24]
    mov ebp, [esp + 28]
    xor ecx, ecx
.edge:
    mov ebx, [esi + ecx * 4]
.wait_edge:
    SINCE_PIT
    cmp ax, bx
    jb .wait_edge
    mov al, 0x0F
    test cl, 1
    jz .write
    mov al, 0x2F
.write:
    mov dx, 0x04D6
    out dx, al
    READ_PIT
    mov bx, bp
    sub bx, ax
    mov [edi + 8 + ecx * 2], bx
    mov bp, ax
    mov ebx, 15
    test ecx, ecx
    jnz .early
    mov ebx, 29
.early:
    SINCE_PIT
    cmp ax, bx
    jb .early
    mov dx, 0x04D2
    in al, dx
    mov [edi + ecx], al
    mov ebx, [esi + ecx * 4 + 4]
    sub ebx, 1
.late:
    SINCE_PIT
    cmp ax, bx
    jb .late
    mov dx, 0x04D2
    in al, dx
    mov [edi + 4 + ecx], al
    inc ecx
    cmp ecx, 4
    jb .edge
    movzx eax, bp
    pop ebp
    pop edi
    pop esi
    pop ebx
    ret
