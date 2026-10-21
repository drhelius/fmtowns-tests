; uint32_t fdc_pio_read(uint8_t* buffer, uint32_t capacity, uint32_t limit, uint32_t settle): reads 0206h at every
; data request of the command just written until BUSY clears; BUSY clear only ends it after settle passes or once it
; was seen set. Returns the bytes seen (only capacity kept), FFFFFFFFh after limit passes of the loop

cpu 386
bits 32

global fdc_pio_read

section .text

fdc_pio_read:
    push ebx
    push esi
    push edi
    push ebp
    mov edi, [esp + 20]
    mov esi, [esp + 24]
    mov ecx, [esp + 28]
    mov ebp, [esp + 32]
    xor ebx, ebx
    mov edx, 0x0200
.loop:
    in al, dx
    test al, 0x02
    jnz .data
    test al, 0x01
    jnz .busy
    test ebp, ebp
    jz .done
    dec ebp
    jmp .count
.busy:
    xor ebp, ebp
.count:
    dec ecx
    jnz .loop
    mov eax, 0xFFFFFFFF
    jmp .out
.data:
    mov dl, 0x06
    in al, dx
    mov dl, 0x00
    cmp ebx, esi
    jae .skip
    mov [edi + ebx], al
.skip:
    inc ebx
    xor ebp, ebp
    jmp .count
.done:
    mov eax, ebx
.out:
    pop ebp
    pop edi
    pop esi
    pop ebx
    ret
