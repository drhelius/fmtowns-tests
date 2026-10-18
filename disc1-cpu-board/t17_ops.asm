; 1.7 instruction stubs: one instruction and a return, run by alu_exec with the registers and flags of an alu_regs_t
; BSWAP is a 486 instruction; on a 386 its stubs raise #UD inside a catch

cpu 486
bits 32

global alu_exec

section .bss

alignb 4
stub_pointer:
    resd 1

section .text

; void alu_exec(void* stub, alu_regs_t* regs): eax, ebx, ecx, edx, esi, edi, eflags in and out
alu_exec:
    push ebp
    push ebx
    push esi
    push edi
    mov eax, [esp + 20]
    mov [stub_pointer], eax
    mov ebp, [esp + 24]
    push dword [ebp + 24]
    mov eax, [ebp + 0]
    mov ebx, [ebp + 4]
    mov ecx, [ebp + 8]
    mov edx, [ebp + 12]
    mov esi, [ebp + 16]
    mov edi, [ebp + 20]
    popfd
    call [stub_pointer]
    pushfd
    mov [ebp + 0], eax
    mov [ebp + 4], ebx
    mov [ebp + 8], ecx
    mov [ebp + 12], edx
    mov [ebp + 16], esi
    mov [ebp + 20], edi
    pop dword [ebp + 24]
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

%macro STUB 2+
global %1
%1:
    %2
    ret
%endmacro

; Cyrix's detection: a Cyrix leaves the flags of the SAHF, so AH reads 02h
global alu_cyrix
alu_cyrix:
    xor ax, ax
    sahf
    mov ax, 5
    mov bl, 2
    div bl
    lahf
    ret

STUB alu_div8, div bl
STUB alu_div16, div bx
STUB alu_div32, div ebx
STUB alu_idiv8, idiv bl
STUB alu_idiv16, idiv bx
STUB alu_idiv32, idiv ebx

STUB alu_mul8, mul bl
STUB alu_mul16, mul bx
STUB alu_mul32, mul ebx
STUB alu_imul8, imul bl
STUB alu_imul16, imul bx
STUB alu_imul32, imul ebx
STUB alu_imul2_16, imul ax, bx
STUB alu_imul2_32, imul eax, ebx
STUB alu_imul3_16, imul ax, bx, 0x5A3C
STUB alu_imul3_32, imul eax, ebx, 0x5A3C96E1

STUB alu_bsf16, bsf ax, bx
STUB alu_bsf32, bsf eax, ebx
STUB alu_bsr16, bsr ax, bx
STUB alu_bsr32, bsr eax, ebx

STUB alu_shl8, shl al, cl
STUB alu_shl16, shl ax, cl
STUB alu_shl32, shl eax, cl
STUB alu_shr8, shr al, cl
STUB alu_shr16, shr ax, cl
STUB alu_shr32, shr eax, cl
STUB alu_sar8, sar al, cl
STUB alu_sar16, sar ax, cl
STUB alu_sar32, sar eax, cl
STUB alu_rol8, rol al, cl
STUB alu_rol16, rol ax, cl
STUB alu_rol32, rol eax, cl
STUB alu_ror8, ror al, cl
STUB alu_ror16, ror ax, cl
STUB alu_ror32, ror eax, cl
STUB alu_rcl8, rcl al, cl
STUB alu_rcl16, rcl ax, cl
STUB alu_rcl32, rcl eax, cl
STUB alu_rcr8, rcr al, cl
STUB alu_rcr16, rcr ax, cl
STUB alu_rcr32, rcr eax, cl
STUB alu_shld16, shld ax, bx, cl
STUB alu_shld32, shld eax, ebx, cl
STUB alu_shrd16, shrd ax, bx, cl
STUB alu_shrd32, shrd eax, ebx, cl

STUB alu_aaa, aaa
STUB alu_aas, aas
STUB alu_daa, daa
STUB alu_das, das
STUB alu_aam10, aam
STUB alu_aam16, aam 16
STUB alu_aam7, aam 7
STUB alu_aam0, aam 0
STUB alu_aad10, aad
STUB alu_aad16, aad 16
STUB alu_aad7, aad 7

STUB alu_bt16, bt ax, bx
STUB alu_bt32, bt eax, ebx
STUB alu_bts16, bts ax, bx
STUB alu_bts32, bts eax, ebx
STUB alu_btr16, btr ax, bx
STUB alu_btr32, btr eax, ebx
STUB alu_btc16, btc ax, bx
STUB alu_btc32, btc eax, ebx

STUB alu_bswap32, bswap eax
STUB alu_bswap16, db 0x66, 0x0F, 0xC8
