; Fork and copy-on-write, from the inside.
;
; The marker byte is written before the fork, so its page is resident and
; gets shared read-only between parent and child. The child overwrites it;
; if COW works, the parent still sees the old value. If the page were
; genuinely shared, the child's write would show up in the parent and the
; parent would print B.

BITS 32
ORG 0x08048000

EXIT    equ 1
FORK    equ 2
WRITE   equ 3
SYSWAIT equ 7       ; WAIT is an x86 mnemonic, nasm won't take it as a label

_start:
    mov byte [marker], 'A'

    mov eax, WRITE
    mov ebx, 1
    mov ecx, before_msg
    mov edx, before_len
    int 0x80

    mov eax, FORK
    int 0x80

    test eax, eax
    jz .child

    ; --- parent ---
    mov eax, SYSWAIT            ; blocks until the child exits
    int 0x80
    push eax                    ; child's exit code

    mov bl, [marker]
    mov [parent_char], bl

    mov eax, WRITE
    mov ebx, 1
    mov ecx, parent_msg
    mov edx, parent_len
    int 0x80

    pop eax
    cmp eax, 9
    jne .badcode

    mov eax, WRITE
    mov ebx, 1
    mov ecx, code_ok
    mov edx, code_ok_len
    int 0x80

    mov eax, EXIT
    xor ebx, ebx
    int 0x80

.badcode:
    mov eax, WRITE
    mov ebx, 1
    mov ecx, code_bad
    mov edx, code_bad_len
    int 0x80

    mov eax, EXIT
    mov ebx, 1
    int 0x80

.child:
    mov byte [marker], 'B'

    mov bl, [marker]
    mov [child_char], bl

    mov eax, WRITE
    mov ebx, 1
    mov ecx, child_msg
    mov edx, child_len
    int 0x80

    mov eax, EXIT
    mov ebx, 9
    int 0x80

marker: db 0

before_msg: db "forktest: before fork, marker=A", 10
before_len  equ $ - before_msg

child_msg:  db "child: marker="
child_char: db "?"
            db 10
child_len   equ $ - child_msg

parent_msg:  db "parent: marker="
parent_char: db "?"
             db 10
parent_len   equ $ - parent_msg

code_ok:     db "parent: child exited 9 as expected", 10
code_ok_len  equ $ - code_ok
code_bad:    db "parent: wrong child exit code", 10
code_bad_len equ $ - code_bad
