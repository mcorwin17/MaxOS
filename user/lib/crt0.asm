; C runtime zero. The kernel enters with [esp]=argc and esp+4 the argv
; array; hand those to main and turn its return value into exit().

BITS 32

section .text

global _start
extern main

_start:
    mov eax, [esp]          ; argc
    lea ecx, [esp + 4]      ; argv
    push ecx
    push eax
    call main

    mov ebx, eax            ; main's return is the exit code
    mov eax, 1              ; SYS_EXIT
    int 0x80
