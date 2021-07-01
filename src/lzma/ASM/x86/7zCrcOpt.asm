; 7zCrcOpt.asm -- CRC32 calculation : optimized version
; 2009-12-12 : Igor Pavlov : Public domain
; 2021-02-07 : Igor Pavlov : Public domain
; 2021-06-28 : Peter Hyman (2021 updates)

%include "7zAsm.asm"

MY_ASM_START

%define rD    r2
%define rN    r7
%define rT    r5

%ifdef x64
	%define num_VAR      r8
	%define table_VAR    r9
%else ; 32 bit CDECL assumed
	%define crc_OFFS    REG_SIZE + 5
	%define data_OFFS   REG_SIZE * crc_OFFS
	%define size_OFFS   REG_SIZE + data_OFFS
	%define table_OFFS  REG_SIZE + size_OFFS
	%define num_VAR     [r4 + data_size]
	%define table_VAR   [r4 + crc_table]
%endif

%define SRCDAT  rN + rD + 4 *

%macro CRC 4  ;CRC macro op:req, dest:req, src:req, t:req
	%1 %2, DWORD [rT + %3 * 4 + 0400h * %4]  ; op      dest, DWORD [rT + src * 4 + 0400h * t]
%endmacro

%macro CRC_XOR 3  ; CRC_XOR macro dest:req, src:req, t:req
	CRC xor, %1, %2, %3
%endmacro

%macro CRC_MOV 3  ; CRC_MOV macro dest:req, src:req, t:req
	CRC mov, %1, %2, %3   ; CRC mov, dest, src, t
%endmacro

%macro CRC1b 0
	movzx   x6, BYTE [rD]
	inc     rD
	movzx   x3, x0_L
	xor     x6, x3
	shr     x0, 8
	CRC     xor, x0, r6, 0
	dec     rN
%endmacro

%macro  MY_PROLOG 1 ; MY_PROLOG macro crc_end:req

%ifdef x64   ; we're only doing linux so don't test for W$ stuff
	MY_PUSH_2_REGS
	mov    x0, REG_ABI_PARAM_0_x   ; x0 = x7
	mov    rT, REG_ABI_PARAM_3     ; r5 = r1
	mov    rN, REG_ABI_PARAM_2     ; r7 = r2
	mov    rD, REG_ABI_PARAM_1     ; r2 = r6
; no non-linux stuff

%else ; not x64
	MY_PUSH_4_REGS
	mov x0, [r4 + crc_OFFS]
	mov rD, [r4 + data_OFFS]
; no W$ fastcall
	mov rN, num_VAR
	mov rT_table_VAR
%endif ; x64 or 32 bit

	test    rN, rN
	jz   near   %1 ; crc_end
%%sl:
	test    rD, 7
	jz     %%sl_end
	CRC1b
	jnz     %%sl
%%sl_end:
	cmp     rN, 16
	jb   near   %1; crc_end
	add     rN, rD
	mov     num_VAR, rN
	sub     rN, 8
	and     rN, NOT 7
	sub     rD, rN
	xor     x0, [SRCDAT 0]
%endmacro

%macro MY_EPILOG 1  ; MY_EPILOG macro crc_end:req
	xor     x0, [SRCDAT 0]
	mov     rD, rN
	mov     rN, num_VAR
	sub     rN, rD
	%1:  ; crc_end:
	test    rN, rN
	jz      %%end ; @F
	CRC1b
	jmp     %1 ; crc_end
%%end:
%ifdef x64
	MY_POP_2_REGS
%else
	MY_POP_4_REGS
%endif
%endmacro

MY_PROC CrcUpdateT8, 4
MY_PROLOG crc_end_8
	mov     x1, [SRCDAT 1]
	align 16
main_loop_8:
	mov     x6, [SRCDAT 2]
	movzx   x3, x1_L
	CRC_XOR x6, r3, 3
	movzx   x3, x1_H
	CRC_XOR x6, r3, 2
	shr     x1, 16
	movzx   x3, x1_L
	movzx   x1, x1_H
	CRC_XOR x6, r3, 1
	movzx   x3, x0_L
	CRC_XOR x6, r1, 0

	mov     x1, [SRCDAT 3]
	CRC_XOR x6, r3, 7
	movzx   x3, x0_H
	shr     x0, 16
	CRC_XOR x6, r3, 6
	movzx   x3, x0_L
	CRC_XOR x6, r3, 5
	movzx   x3, x0_H
	CRC_MOV x0, r3, 4
	xor     x0, x6
	add     rD, 8
	jnz     main_loop_8

MY_EPILOG crc_end_8
MY_ENDP

; T4 CRC deleted

; end

%ifidn __OUTPUT_FORMAT__,elf
section .note.GNU-stack noalloc noexec nowrite progbits
%endif
