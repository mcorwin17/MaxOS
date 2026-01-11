; Reads a file the HOST wrote, through fd syscalls, the VFS, FAT16, the
; block cache and the ATA driver - the whole storage stack from ring 3.

BITS 32
ORG 0x08048000

EXIT  equ 1
WRITE equ 3
OPEN  equ 12
READ  equ 13
CLOSE equ 14

_start:
    mov eax, OPEN
    mov ebx, path
    int 0x80

    cmp eax, 0
    jl .fail
    mov esi, eax                ; fd

    mov eax, READ
    mov ebx, esi
    mov ecx, buf
    mov edx, 256
    int 0x80

    cmp eax, 0
    jle .fail
    mov edi, eax                ; bytes read

    mov eax, WRITE
    mov ebx, 1
    mov ecx, prefix
    mov edx, prefix_len
    int 0x80

    mov eax, WRITE
    mov ebx, 1
    mov ecx, buf
    mov edx, edi
    int 0x80

    mov eax, CLOSE
    mov ebx, esi
    int 0x80

    mov eax, EXIT
    xor ebx, ebx
    int 0x80

.fail:
    mov eax, WRITE
    mov ebx, 1
    mov ecx, fail_msg
    mov edx, fail_len
    int 0x80

    mov eax, EXIT
    mov ebx, 1
    int 0x80

path:       db "/HELLO.TXT", 0
prefix:     db "readfile got: "
prefix_len  equ $ - prefix
fail_msg:   db "readfile: open or read failed", 10
fail_len    equ $ - fail_msg

buf:        times 256 db 0
