; 2.4 CRTC start and timing registers
; uint32_t crtc_sync_scan(uint32_t polls, uint32_t* changes, uint32_t capacity, uint32_t mask, uint32_t fmr): FDA0h
; bits 1-0, FR's VSYNC and HSYNC (0443h, index 1Eh) as bits 2 and 3 and, when fmr is set, CFF86h's VSYNC and HSYNC as
; bits 4 and 5; stores poll index << 8 | masked state on every change and returns the polls done
; void crtc_sync_stats(uint32_t source, uint32_t polls, uint32_t* counts): one source's VSYNC and HSYNC (0 FDA0h, 1 FR,
; 2 CFF86h); adds the polls that read HSYNC set to counts[0], HSYNC rises to counts[1] and VSYNC rises to counts[2]
; uint32_t crtc_fr_scan(uint32_t polls, uint32_t* changes, uint32_t capacity, uint32_t mask): FR's high byte (0443h,
; index 1Eh) masked, poll index << 8 | state on every change; returns the polls done
; void crtc_line_counts(uint32_t polls, uint32_t* counts): FR's DSPTH0 (high byte bit 4) alone, in a tight loop; adds
; the polls that read it set to counts[0] and its rises to counts[1]
; void crtc_field_scan(uint32_t polls, uint16_t* times, uint32_t fields): FR's high byte until the polls run out or
; the fields are full, from the first VSYNC rise; per field six readings of the 0026h counter: VSYNC rise, VSYNC fall,
; the first two HSYNC rises after the fall, DSPTV0 rise and fall. The caller fills times with FFFFh

cpu 386
bits 32

global crtc_sync_scan
global crtc_sync_stats
global crtc_fr_scan
global crtc_line_counts
global crtc_field_scan

section .text

crtc_sync_scan:
    push ebx
    push esi
    push edi
    push ebp
    mov ebp, [esp + 20]
    mov edi, [esp + 24]
    mov ecx, [esp + 28]
    mov bh, [esp + 32]
    mov esi, ebp
    mov bl, 0xFF
.loop:
    mov dx, 0xFDA0
    in al, dx
    and al, 3
    mov ah, al
    mov dx, 0x0443
    in al, dx
    test al, 4
    jz .fr_hsync
    or ah, 0x04
.fr_hsync:
    test al, 2
    jz .fmr
    or ah, 0x08
.fmr:
    test byte [esp + 36], 1
    jz .state
    mov al, [0x000CFF86]
    test al, 4
    jz .fmr_hsync
    or ah, 0x10
.fmr_hsync:
    test al, 0x80
    jz .state
    or ah, 0x20
.state:
    mov al, ah
    and al, bh
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

; al: VSYNC in bit 0 and HSYNC in bit 1; bl the previous state, ecx the polls with HSYNC set, ebp the HSYNC rises
%macro STATS_TAIL 1
    test al, 2
    jz %%low
    inc ecx
%%low:
    mov ah, bl
    not ah
    and ah, al
    mov bl, al
    test ah, 2
    jz %%no_hsync
    inc ebp
%%no_hsync:
    test ah, 1
    jz %%no_vsync
    inc dword [edi + 8]
%%no_vsync:
    dec esi
    jnz %1
%endmacro

crtc_sync_stats:
    push ebx
    push esi
    push edi
    push ebp
    mov eax, [esp + 20]
    mov esi, [esp + 24]
    mov edi, [esp + 28]
    xor ecx, ecx
    xor ebp, ebp
    mov bl, 0xFF
    cmp eax, 1
    je .fr
    ja .fmr
    mov dx, 0xFDA0
.fda0:
    in al, dx
    and al, 3
    STATS_TAIL .fda0
    jmp .done
.fr:
    mov dx, 0x0443
.fr_loop:
    in al, dx
    mov ah, al
    shr al, 2
    and al, 1
    and ah, 2
    or al, ah
    STATS_TAIL .fr_loop
    jmp .done
.fmr:
    mov al, [0x000CFF86]
    mov ah, al
    shr al, 2
    and al, 1
    shr ah, 6
    and ah, 2
    or al, ah
    STATS_TAIL .fmr
.done:
    add [edi], ecx
    add [edi + 4], ebp
    pop ebp
    pop edi
    pop esi
    pop ebx
    ret

crtc_fr_scan:
    push ebx
    push esi
    push edi
    push ebp
    mov ebp, [esp + 20]
    mov edi, [esp + 24]
    mov ecx, [esp + 28]
    mov bh, [esp + 32]
    mov esi, ebp
    mov bl, 0xFF
    mov edx, 0x0443
.loop:
    in al, dx
    and al, bh
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

crtc_line_counts:
    push ebx
    push esi
    push edi
    push ebp
    mov esi, [esp + 20]
    mov edi, [esp + 24]
    xor ecx, ecx
    xor ebp, ebp
    mov bl, 0x10
    mov edx, 0x0443
.loop:
    in al, dx
    and al, 0x10
    jz .low
    inc ecx
    cmp bl, 0
    jne .same
    inc ebp
.same:
    mov bl, al
    dec esi
    jnz .loop
    jmp .done
.low:
    mov bl, al
    dec esi
    jnz .loop
.done:
    add [edi], ecx
    add [edi + 4], ebp
    pop ebp
    pop edi
    pop esi
    pop ebx
    ret

; bl the state, cl the previous one, ax the time of a change; bh: bit 0 a field is open, bit 1 counting HSYNC rises
; after the VSYNC fall, bit 2 the first one seen
crtc_field_scan:
    push ebx
    push esi
    push edi
    push ebp
    mov esi, [esp + 20]
    mov edi, [esp + 24]
    mov ebp, [esp + 28]
    mov edx, 0x0443
    in al, dx
    and al, 0x46
    mov bl, al
    xor bh, bh
.loop:
    in al, dx
    and al, 0x46
    cmp al, bl
    jne .change
.next:
    dec esi
    jnz .loop
    jmp .done
.change:
    mov cl, bl
    mov bl, al
    mov dx, 0x0026
    in ax, dx
    mov dx, 0x0443
    test bl, 4
    jz .vsync_low
    test cl, 4
    jnz .hsync
    test bh, 1
    jz .open
    add edi, 12
    dec ebp
    jz .done
.open:
    mov bh, 1
    mov [edi], ax
    jmp .next
.vsync_low:
    test cl, 4
    jz .hsync
    test bh, 1
    jz .next
    mov [edi + 2], ax
    or bh, 2
    jmp .window
.hsync:
    test bh, 2
    jz .window
    test bl, 2
    jz .window
    test cl, 2
    jnz .window
    test bh, 4
    jnz .second
    mov [edi + 4], ax
    or bh, 4
    jmp .window
.second:
    mov [edi + 6], ax
    and bh, 0xF9
.window:
    test bh, 1
    jz .next
    test bl, 0x40
    jz .window_low
    test cl, 0x40
    jnz .next
    mov [edi + 8], ax
    jmp .next
.window_low:
    test cl, 0x40
    jz .next
    mov [edi + 10], ax
    jmp .next
.done:
    pop ebp
    pop edi
    pop esi
    pop ebx
    ret
