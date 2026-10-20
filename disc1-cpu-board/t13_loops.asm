; 1.3 bus timing loops: one instruction unrolled, counted in EBP, timed with PIT counter 0 (307.2 kHz)
; EDI points at the memory under test, EDX holds the port or the second address and ESI the second RAM buffer

cpu 386
bits 32

PIT0_COUNTER0       equ 0x0040
PIT0_CONTROL        equ 0x0046
UNROLL              equ 16

global bus_time
global bus_ram_a
global bus_ram_b

section .bss

alignb 16
bus_ram_a:
    resb 4096
bus_ram_b:
    resb 4096
start_count:
    resd 1
target:
    resd 1
saved_esp:
    resd 1

section .text

%macro TIMED 2
global %1
align 16
%1:
.loop:
%rep UNROLL
    %2
%endrep
    dec ebp
    jnz .loop
    ret
%endmacro

; uint32_t bus_time(void* loop, uint32_t iterations, uint32_t edi, uint32_t edx)
; Returns the PIT ticks the loop took
bus_time:
    push ebp
    push ebx
    push esi
    push edi
    mov eax, [esp + 20]
    mov [target], eax
    mov ebp, [esp + 24]
    mov edi, [esp + 28]
    mov edx, [esp + 32]
    mov esi, bus_ram_b
    mov [saved_esp], esp
    mov al, 0
    out PIT0_CONTROL, al
    in al, PIT0_COUNTER0
    mov ah, al
    in al, PIT0_COUNTER0
    xchg al, ah
    movzx eax, ax
    mov [start_count], eax
    xor eax, eax
    xor ebx, ebx
    call [target]
    mov esp, [saved_esp]
    mov al, 0
    out PIT0_CONTROL, al
    in al, PIT0_COUNTER0
    mov ah, al
    in al, PIT0_COUNTER0
    xchg al, ah
    movzx eax, ax
    mov ebx, [start_count]
    sub ebx, eax
    and ebx, 0xFFFF
    mov eax, ebx
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

TIMED bus_empty, {}
TIMED bus_nop, {nop}
TIMED bus_mov_rr, {mov eax, ebx}
TIMED bus_add_rr, {add eax, ebx}
TIMED bus_add_imm8, {add eax, 1}

TIMED bus_load8, {mov al, [edi]}
TIMED bus_store8, {mov [edi], al}
TIMED bus_load16, {mov ax, [edi]}
TIMED bus_store16, {mov [edi], ax}
TIMED bus_load32, {mov eax, [edi]}
TIMED bus_store32, {mov [edi], eax}
TIMED bus_load32_1, {mov eax, [edi + 1]}
TIMED bus_load32_2, {mov eax, [edi + 2]}
TIMED bus_load32_3, {mov eax, [edi + 3]}
TIMED bus_load16_1, {mov ax, [edi + 1]}

global bus_push
align 16
bus_push:
.loop:
%rep UNROLL
    push eax
%endrep
    add esp, UNROLL * 4
    dec ebp
    jnz .loop
    ret

global bus_pop
align 16
bus_pop:
.loop:
    sub esp, UNROLL * 4
%rep UNROLL
    pop eax
%endrep
    dec ebp
    jnz .loop
    ret

%macro STRING_LOOP 4
global %1
align 16
%1:
.loop:
    mov esi, bus_ram_a
    mov edi, bus_ram_b
    mov ecx, %3
    rep %2
    dec ebp
    jnz .loop
    ret
%endmacro

STRING_LOOP bus_movsd, movsd, 1024, 0
STRING_LOOP bus_movsw, movsw, 2048, 0
STRING_LOOP bus_movsb, movsb, 4096, 0

global bus_stosd
align 16
bus_stosd:
.loop:
    mov edi, bus_ram_b
    mov ecx, 1024
    rep stosd
    dec ebp
    jnz .loop
    ret

global bus_lodsd
align 16
bus_lodsd:
.loop:
    mov esi, bus_ram_a
%rep UNROLL
    lodsd
%endrep
    dec ebp
    jnz .loop
    ret

TIMED bus_mov_imm32, {mov eax, 0x12345678}
TIMED bus_add_imm32, {add ebx, 0x12345678}
TIMED bus_jmp, {db 0xEB, 0x00}
global bus_jmp_add
align 16
bus_jmp_add:
.loop:
%rep UNROLL
    db 0xEB, 0x00
    add eax, ebx
%endrep
    dec ebp
    jnz .loop
    ret

global bus_load_add_imm32
align 16
bus_load_add_imm32:
.loop:
%rep UNROLL
    mov eax, [edi]
    add ebx, 0x12345678
%endrep
    dec ebp
    jnz .loop
    ret

TIMED bus_in8_30, {in al, 0x30}
TIMED bus_in8, {in al, dx}
TIMED bus_out8, {out dx, al}
TIMED bus_in16, {in ax, dx}

; DRAM patterns: two accesses per unrolled unit, EDI and EDX a stride apart
global bus_alt_load16
align 16
bus_alt_load16:
.loop:
%rep UNROLL
    mov ax, [edi]
    mov ax, [edx]
%endrep
    dec ebp
    jnz .loop
    ret

TIMED bus_own_code_load16, {mov ax, [edi]}

global bus_rep_lodsw
align 16
bus_rep_lodsw:
    mov ebx, edi
.loop:
    mov esi, ebx
    mov ecx, 2048
    rep lodsw
    dec ebp
    jnz .loop
    ret

global bus_rep_lodsd
align 16
bus_rep_lodsd:
    mov ebx, edi
.loop:
    mov esi, ebx
    mov ecx, 1024
    rep lodsd
    dec ebp
    jnz .loop
    ret

global bus_rep_stosw
align 16
bus_rep_stosw:
    mov ebx, edi
.loop:
    mov edi, ebx
    mov ecx, 2048
    rep stosw
    dec ebp
    jnz .loop
    ret

global bus_movsd_near
align 16
bus_movsd_near:
    mov ebx, edi
.loop:
    mov esi, ebx
    lea edi, [ebx + 512]
    mov ecx, 128
    rep movsd
    dec ebp
    jnz .loop
    ret

global bus_load_store16
align 16
bus_load_store16:
.loop:
%rep UNROLL
    mov ax, [edi]
    mov [edi], ax
%endrep
    dec ebp
    jnz .loop
    ret

TIMED bus_add_m16, {add [edi], ax}

TIMED bus_call_edi, {call edi}

global bus_ret
align 16
bus_ret:
    ret

global bus_store_load32
align 16
bus_store_load32:
.loop:
%rep UNROLL
    mov [edi], eax
    mov ebx, [edx]
%endrep
    dec ebp
    jnz .loop
    ret

global bus_alt_store16
align 16
bus_alt_store16:
.loop:
%rep UNROLL
    mov [edi], ax
    mov [edx], ax
%endrep
    dec ebp
    jnz .loop
    ret

; 4 KB of straight code, so the fetches cross DRAM pages
global bus_code_4k
align 16
bus_code_4k:
.loop:
%rep 2048
    mov eax, ebx
%endrep
    dec ebp
    jnz .loop
    ret
