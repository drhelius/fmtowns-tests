; Tight FDA0h loops for disc 2, registers only so the poll interval stays below the shortest HSYNC pulse
; uint32_t video_count_hsyncs(uint32_t max_polls): bit 1 rises until bit 0 rises, FFFFFFFFh after max_polls
; uint32_t video_time_hsyncs(uint32_t rises, uint32_t max_polls, uint32_t* high): polls until that many bit 1 rises,
; 0 after max_polls; high gets the polls that read bit 1 set

cpu 386
bits 32

global video_count_hsyncs
global video_time_hsyncs
global video_wait_bit
global video_wait_port
global video_scan
global video_count_edges
global video_spin
global video_clock_loop

section .text

video_count_hsyncs:
    push ebx
    push esi
    mov esi, [esp + 12]
    xor ecx, ecx
    mov edx, 0xFDA0
    in al, dx
    mov bl, al
.loop:
    in al, dx
    mov ah, al
    not bl
    and bl, al
    test bl, 2
    jz .no_hsync
    inc ecx
.no_hsync:
    test bl, 1
    jnz .done
    mov bl, ah
    dec esi
    jnz .loop
    mov ecx, 0xFFFFFFFF
.done:
    mov eax, ecx
    pop esi
    pop ebx
    ret

video_time_hsyncs:
    push ebx
    push esi
    push edi
    mov ecx, [esp + 16]
    mov esi, [esp + 20]
    xor edi, edi
    xor eax, eax
    mov edx, 0xFDA0
    in al, dx
    and al, 2
    mov bl, al
.loop:
    in al, dx
    and eax, 2
    add edi, eax
    mov bh, al
    not bl
    and bl, al
    jz .next
    dec ecx
    jz .done
.next:
    mov bl, bh
    dec esi
    jnz .loop
    xor eax, eax
    jmp .out
.done:
    mov eax, [esp + 20]
    sub eax, esi
    inc eax
.out:
    shr edi, 1
    mov ecx, [esp + 24]
    mov [ecx], edi
    pop edi
    pop esi
    pop ebx
    ret

; uint32_t video_wait_bit(uint32_t mask, uint32_t value, uint32_t max_polls): polls FDA0h until the masked bits read
; the value; returns the polls left, 0 after max_polls
video_wait_bit:
    push ebx
    mov ebx, [esp + 8]
    mov ah, [esp + 12]
    mov ecx, [esp + 16]
    mov edx, 0xFDA0
.loop:
    in al, dx
    and al, bl
    cmp al, ah
    je .done
    dec ecx
    jnz .loop
.done:
    mov eax, ecx
    pop ebx
    ret

; uint32_t video_wait_port(uint32_t port, uint32_t mask, uint32_t value, uint32_t max_polls): video_wait_bit on
; another port
video_wait_port:
    push ebx
    mov edx, [esp + 8]
    mov ebx, [esp + 12]
    mov ah, [esp + 16]
    mov ecx, [esp + 20]
.loop:
    in al, dx
    and al, bl
    cmp al, ah
    je .done
    dec ecx
    jnz .loop
.done:
    mov eax, ecx
    pop ebx
    ret

; void video_clock_loop(uint32_t count): count times 32 AAM, 17 clocks each on the 386 and 15 on the 486
video_clock_loop:
    mov ecx, [esp + 4]
.loop:
    times 32 aam
    dec ecx
    jnz .loop
    ret

; uint32_t video_scan(uint32_t max_polls, uint32_t* changes, uint32_t capacity, uint32_t mask): the masked state of
; FDA0h bits 0-1 and, for mask bit 2, the slave IRR bit 3 (IRQ11), storing poll index << 8 | state on every change,
; the first poll included; returns the polls done
video_scan:
    push ebx
    push esi
    push edi
    push ebp
    mov ebp, [esp + 20]
    mov edi, [esp + 24]
    mov ecx, [esp + 28]
    mov bh, [esp + 32]
    mov esi, ebp
    mov al, 0x0A
    out 0x10, al
    mov bl, 0xFF
    mov edx, 0xFDA0
    test bh, 4
    jnz .irr_loop
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
    call .store
    jnz .next
    jmp .done
.irr_loop:
    in al, dx
    and al, 3
    mov ah, al
    in al, 0x10
    and al, 8
    shr al, 1
    or al, ah
    and al, bh
    cmp al, bl
    jne .irr_change
.irr_next:
    dec esi
    jnz .irr_loop
    jmp .done
.irr_change:
    call .store
    jnz .irr_next
.done:
    mov eax, ebp
    sub eax, esi
    pop ebp
    pop edi
    pop esi
    pop ebx
    ret
.store:
    mov bl, al
    mov eax, ebp
    sub eax, esi
    shl eax, 8
    mov al, bl
    mov [edi], eax
    add edi, 4
    dec ecx
    ret

; void video_count_edges(uint32_t polls, uint32_t* counts): adds bit 0 rises to counts[0], bit 1 rises to counts[1]
; and IRQ11 requests in the slave IRR to counts[2], clearing each through 05CAh; ORs every FDA0h read into
; counts[3] and ANDs it into counts[4]; counts[5] carries the previous read from call to call
video_count_edges:
    push ebx
    push esi
    push edi
    mov esi, [esp + 16]
    mov edi, [esp + 20]
    mov al, 0x0A
    out 0x10, al
    mov bl, [edi + 20]
    mov bh, [edi + 12]
    mov cl, [edi + 16]
    mov edx, 0xFDA0
.loop:
    in al, dx
    or bh, al
    and cl, al
    mov ch, al
    not bl
    and bl, al
    test bl, 1
    jz .no_vsync
    inc dword [edi]
.no_vsync:
    test bl, 2
    jz .no_hsync
    inc dword [edi + 4]
.no_hsync:
    mov bl, ch
    in al, 0x10
    test al, 8
    jz .no_request
    inc dword [edi + 8]
    mov dx, 0x05CA
    out dx, al
    mov dx, 0xFDA0
.no_request:
    dec esi
    jnz .loop
    mov [edi + 12], bh
    mov [edi + 16], cl
    mov [edi + 20], bl
    pop edi
    pop esi
    pop ebx
    ret

; void video_spin(uint32_t passes): DEC ECX / JNZ that many times
video_spin:
    mov ecx, [esp + 4]
    or ecx, ecx
    jz .done
.loop:
    dec ecx
    jnz .loop
.done:
    ret
