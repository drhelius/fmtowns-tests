; 2.9 Sprite transfer timing
; uint32_t sprite_scan(uint32_t max_polls, uint32_t* changes, uint32_t capacity): FDA0h bit 0 as bit 0 and 044Ch
; bits 1-0 (BUSY, PAGE) as bits 2-1, storing poll index << 8 | state on every change; returns the polls done
; uint32_t sprite_contend(uint32_t kind, uint32_t address, uint32_t dwords, uint32_t max_chunks): REP STOSD (kind 0)
; or REP LODSD (kind 1) of dwords at address, over and over, until 044Ch bit 1 reads 0; returns the chunks run, 0
; after max_chunks

cpu 386
bits 32

global sprite_scan
global sprite_contend

section .text

sprite_scan:
    push ebx
    push esi
    push edi
    push ebp
    mov ebp, [esp + 20]
    mov edi, [esp + 24]
    mov ecx, [esp + 28]
    mov esi, ebp
    mov bl, 0xFF
.loop:
    mov dx, 0xFDA0
    in al, dx
    and al, 1
    mov ah, al
    mov dx, 0x044C
    in al, dx
    and al, 3
    add al, al
    or al, ah
    cmp al, bl
    jne .change
.next:
    dec esi
    jnz .loop
    jmp .done
.change:
    mov bl, al
    mov eax, ebp
    sub eax, esi
    shl eax, 8
    mov al, bl
    mov [edi], eax
    add edi, 4
    dec ecx
    jnz .next
.done:
    mov eax, ebp
    sub eax, esi
    pop ebp
    pop edi
    pop esi
    pop ebx
    ret

sprite_contend:
    push ebx
    push esi
    push edi
    push ebp
    mov ebx, [esp + 20]
    mov ebp, [esp + 32]
    cld
.loop:
    mov ecx, [esp + 28]
    test ebx, ebx
    jnz .load
    mov edi, [esp + 24]
    xor eax, eax
    rep stosd
    jmp .poll
.load:
    mov esi, [esp + 24]
    rep lodsd
.poll:
    mov dx, 0x044C
    in al, dx
    test al, 2
    jz .done
    dec ebp
    jnz .loop
    xor eax, eax
    jmp .out
.done:
    mov eax, [esp + 32]
    sub eax, ebp
    inc eax
.out:
    pop ebp
    pop edi
    pop esi
    pop ebx
    ret
