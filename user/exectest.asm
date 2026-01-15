; exec: replace this program with hello, keeping the pid. If it works, the
; next thing this process does is hello's first write, and its exit code is
; hello's 42. If exec ever returns, it failed.

BITS 32
ORG 0x08048000

EXIT  equ 1
WRITE equ 3
EXEC  equ 8

_start:
    mov eax, WRITE
    mov ebx, 1
    mov ecx, msg
    mov edx, msg_len
    int 0x80

    mov eax, EXEC
    mov ebx, name
    xor ecx, ecx        ; no argument string
    int 0x80

    ; still here means exec failed
    mov eax, WRITE
    mov ebx, 1
    mov ecx, fail_msg
    mov edx, fail_len
    int 0x80

    mov eax, EXIT
    mov ebx, 1
    int 0x80

name:     db "hello", 0

msg:      db "exectest: replacing myself with hello", 10
msg_len   equ $ - msg
fail_msg: db "BUG: exec returned", 10
fail_len  equ $ - fail_msg
