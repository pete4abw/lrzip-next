; 7zAsm.asm -- ASM macros
; 2009-12-12 : Igor Pavlov : Public domain
; 2018-02-03 : Igor Pavlov : Public domain
; 2011-10-12 : P7ZIP       : Public domain
; 2020-01-02 : Peter Hyman (integrating 2018 updates)
%define NOT ~

%macro MY_ASM_START 0
  SECTION .text
%endmacro

%macro MY_PROC 2 ; macro name:req, numParams:req
  align 16
  %define proc_numParams %2 ; numParams
    global %1
    global _%1
    %1:
    _%1:
%endmacro

%macro  MY_ENDP 0
  %ifdef x64
    ret
    ; proc_name ENDP
  %else
    ret ; (proc_numParams - 2) * 4
  %endif
%endmacro

%ifdef x64
  REG_SIZE equ 8
  REG_LOGAR_SIZE equ 3
%else
  REG_SIZE equ 4
  REG_LOGAR_SIZE equ 2
%endif

  %define x0  EAX
  %define x1  ECX
  %define x2  EDX
  %define x3  EBX
  %define x4  ESP
  %define x5  EBP
  %define x6  ESI
  %define x7  EDI

  %define x0_W AX
  %define x1_W CX
  %define x2_W DX
  %define x3_W BX

  %define x5_W BP
  %define x6_W SI
  %define x7_W DI

  %define x0_L  AL
  %define x1_L  CL
  %define x2_L  DL
  %define x3_L  BL

  %define x0_H  AH
  %define x1_H  CH
  %define x2_H  DH
  %define x3_H  BH

%ifdef x64
  %define x5_L BPL
  %define x6_L SIL
  %define x7_L DIL

  %define r0  RAX
  %define r1  RCX
  %define r2  RDX
  %define r3  RBX
  %define r4  RSP
  %define r5  RBP
  %define r6  RSI
  %define r7  RDI
  %define x8 r8d
  %define x9 r9d
  %define x10 r10d
  %define x11 r11d
  %define x12 r12d
  %define x13 r13d
  %define x14 r14d
  %define x15 r15d
%else
  %define r0  x0
  %define r1  x1
  %define r2  x2
  %define r3  x3
  %define r4  x4
  %define r5  x5
  %define r6  x6
 %define  r7  x7
%endif

%macro MY_PUSH_4_REGS 0
    push    r3
    push    r5
    push    r6
    push    r7
%endmacro

%macro MY_POP_4_REGS 0
    pop     r7
    pop     r6
    pop     r5
    pop     r3
%endmacro

; Needed for ASM LZMA Decompress
%ifdef x64

%define REG_PARAM_0  r1
%define REG_PARAM_1  r2
%define REG_PARAM_2  r8
%define REG_PARAM_3  r9

%macro MY_PUSH_PRESERVED_REGS 0
    MY_PUSH_4_REGS
    push    r12
    push    r13
    push    r14
    push    r15
%endmacro


%macro MY_POP_PRESERVED_REGS 0
    pop     r15
    pop     r14
    pop     r13
    pop     r12
    MY_POP_4_REGS
%endmacro

%endif

