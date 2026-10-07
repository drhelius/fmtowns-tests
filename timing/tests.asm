; Instruction timing measured with PIT 0 counter 0 (307.2 kHz)
; Every test runs ITERATIONS loops of one instruction unrolled UNROLL times
; g_results holds the elapsed ticks of each test, the first one is the empty loop

cpu 386
bits 32

PIT0_COUNTER0       equ 0x0040
PIT0_CONTROL        equ 0x0046
FMR_WRITE_PLANES    equ 0x000CFF81
FMR_HIDDEN_LINES    equ 0x000C7D00
ITERATIONS          equ 1024
UNROLL              equ 16

global run_tests
global g_results

section .bss

alignb 4
g_results:
    resd 8
ram_buffer:
    resb 64

section .text

%macro LOOP_TEST 2
align 16
%1:
.loop:
%rep UNROLL
    %2
%endrep
    dec ecx
    jnz .loop
    ret
%endmacro

run_tests:
    pushad

    ; Mode 2, binary, full 65536 count
    mov al, 0x34
    out PIT0_CONTROL, al
    xor al, al
    out PIT0_COUNTER0, al
    out PIT0_COUNTER0, al

    ; Plane writes reach VRAM on every plane, below the 400 displayed lines
    mov byte [FMR_WRITE_PLANES], 0x0F
    mov esi, ram_buffer
    mov edi, FMR_HIDDEN_LINES

    mov ebx, loop_empty
    call measure
    mov [g_results + 0], eax

    mov ebx, loop_nop
    call measure
    mov [g_results + 4], eax

    mov ebx, loop_load32
    call measure
    mov [g_results + 8], eax

    mov ebx, loop_store32
    call measure
    mov [g_results + 12], eax

    mov ebx, loop_load16
    call measure
    mov [g_results + 16], eax

    mov ebx, loop_load32_odd
    call measure
    mov [g_results + 20], eax

    mov ebx, loop_vram_store8
    call measure
    mov [g_results + 24], eax

    mov ebx, loop_vram_load8
    call measure
    mov [g_results + 28], eax

    popad
    ret

; EBX = loop to time. Returns the elapsed ticks in EAX
measure:
    call read_counter
    movzx ebp, ax
    mov ecx, ITERATIONS
    call ebx
    call read_counter
    movzx eax, ax
    sub ebp, eax
    and ebp, 0xFFFF
    mov eax, ebp
    ret

; Latches counter 0 and returns its count in AX
read_counter:
    xor al, al
    out PIT0_CONTROL, al
    in al, PIT0_COUNTER0
    mov ah, al
    in al, PIT0_COUNTER0
    xchg al, ah
    ret

LOOP_TEST loop_empty, {}
LOOP_TEST loop_nop, {nop}
LOOP_TEST loop_load32, {mov eax, [esi]}
LOOP_TEST loop_store32, {mov [esi], eax}
LOOP_TEST loop_load16, {mov ax, [esi]}
LOOP_TEST loop_load32_odd, {mov eax, [esi + 1]}
LOOP_TEST loop_vram_store8, {mov [edi], al}
LOOP_TEST loop_vram_load8, {mov al, [edi]}
