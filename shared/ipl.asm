; Boot record shared by the floppy and CD images
; The BIOS loads the first sector to B000:0000 and far calls B000:0004 in real mode
; with BL = boot device (2 floppy, 8 CD) and BH = unit. RETF hands the boot back to the BIOS
; The payload follows the boot record on the media and is loaded to PAYLOAD_BASE
; It is entered in flat 32-bit protected mode, interrupts off, EBX = boot device

cpu 386
bits 16
org 0

%include "layout.inc"

DISK_BIOS_SEGMENT   equ 0xFFFB
DISK_BIOS_OFFSET    equ 0x0014
DEVICE_FLOPPY       equ 2
DEVICE_CD           equ 8
CD_CHUNK            equ 16
FLOPPY_TRACK        equ 8
RETRIES             equ 3

ipl:
    db "IPL4"
    jmp short main
    db "FMTOWNS-TESTS"

    ; IO.SYS location dwords, read by the UX and Marty firmware
    times 0x20 - ($ - $$) db 0
    dd 1, 1

    ; Patched by mkimage.py with the payload size in sectors of this medium
    times IPL_PAYLOAD_SECTORS_OFFSET - ($ - $$) db 0
payload_sectors:
    dw 0

main:
    cli
    cld
    push ds
    push es
    push cs
    pop ds
    mov [boot_device], bx
    mov ax, [payload_sectors]
    mov [remaining], ax
    mov word [destination], PAYLOAD_BASE >> 4
    mov word [lba], 1
    cmp bl, DEVICE_CD
    je load_cd
    cmp bl, DEVICE_FLOPPY
    je load_floppy

decline:
    pop es
    pop ds
    retf

; 2048-byte logical sectors, read in chunks that stay inside one segment
load_cd:
    cmp word [remaining], 0
    je loaded
    mov bx, [remaining]
    cmp bx, CD_CHUNK
    jbe .count
    mov bx, CD_CHUNK
.count:
    mov [count], bx
    mov byte [retries], RETRIES
.read:
    mov ax, [destination]
    mov dx, [lba]
    mov bx, [count]
    push ds
    mov ds, ax
    xor di, di
    xor cx, cx
    mov ax, 0x05C0
    call DISK_BIOS_SEGMENT:DISK_BIOS_OFFSET
    pop ds
    jnc .next
    dec byte [retries]
    jnz .read
    jmp decline
.next:
    mov bx, [count]
    add [lba], bx
    sub [remaining], bx
    shl bx, 7
    add [destination], bx
    jmp load_cd

; 1024-byte sectors, 8 per track and two heads, read up to the end of each track
load_floppy:
    mov al, bh
    or al, 0x20
    mov [device], al
    mov ah, 0x03
    xor ch, ch
    call DISK_BIOS_SEGMENT:DISK_BIOS_OFFSET
.track:
    cmp word [remaining], 0
    je loaded
    mov ax, [lba]
    and ax, FLOPPY_TRACK - 1
    mov bx, FLOPPY_TRACK
    sub bx, ax
    cmp bx, [remaining]
    jbe .count
    mov bx, [remaining]
.count:
    mov [count], bx
    mov byte [retries], RETRIES
.read:
    mov ax, [lba]
    mov dl, al
    and dl, FLOPPY_TRACK - 1
    inc dl
    shr ax, 3
    mov dh, al
    and dh, 1
    shr ax, 1
    mov cx, ax
    mov bx, [count]
    mov si, [destination]
    mov al, [device]
    push ds
    mov ds, si
    xor di, di
    mov ah, 0x05
    call DISK_BIOS_SEGMENT:DISK_BIOS_OFFSET
    pop ds
    jnc .next
    dec byte [retries]
    jnz .read
    jmp decline
.next:
    mov bx, [count]
    add [lba], bx
    sub [remaining], bx
    shl bx, 6
    add [destination], bx
    jmp .track

loaded:
    mov al, 0xFF
    out 0x02, al
    out 0x12, al
    o32 lgdt [gdt_descriptor]
    mov eax, cr0
    or al, 1
    mov cr0, eax
    jmp dword 0x08:(IPL_BASE + protected)

bits 32

protected:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, STACK_TOP
    movzx ebx, word [IPL_BASE + boot_device]
    mov eax, PAYLOAD_BASE
    jmp eax

align 8

gdt:
    dq 0
    dq 0x00CF9A000000FFFF
    dq 0x00CF92000000FFFF
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt - 1
    dd IPL_BASE + gdt

boot_device:    dw 0
remaining:      dw 0
destination:    dw 0
lba:            dw 0
count:          dw 0
device:         db 0
retries:        db 0

%if $ - $$ > 1024
%error "The boot record must fit in one 1024-byte floppy sector"
%endif
