; Installs a SIGINT handler and waits to be interrupted.
;
; The handler runs, prints, and RETURNS - through the restorer stub, whose
; sigreturn puts the interrupted context back. The main loop then notices
; the flag and prints again from resumed code. Both lines appearing is the
; whole round trip: deliver, handle, sigreturn, resume.

BITS 32
ORG 0x08048000

EXIT      equ 1
WRITE     equ 3
SLEEP     equ 5
SIGNAL    equ 9
SIGRETURN equ 11

SIGINT    equ 2

_start:
    ; signal(SIGINT, handler, restorer)
    mov eax, SIGNAL
    mov ebx, SIGINT
    mov ecx, handler
    mov edx, restorer
    int 0x80

    mov eax, WRITE
    mov ebx, 1
    mov ecx, ready_msg
    mov edx, ready_len
    int 0x80

.wait_for_it:
    mov eax, SLEEP
    mov ebx, 100
    int 0x80

    cmp byte [caught], 0
    je .wait_for_it

    mov eax, WRITE
    mov ebx, 1
    mov ecx, resumed_msg
    mov edx, resumed_len
    int 0x80

    mov eax, EXIT
    mov ebx, 5
    int 0x80

handler:                        ; handler(sig) - sig is on the stack, unused
    mov byte [caught], 1

    mov eax, WRITE
    mov ebx, 1
    mov ecx, caught_msg
    mov edx, caught_len
    int 0x80

    ret                         ; into the restorer below

restorer:
    mov eax, SIGRETURN
    int 0x80
    ; not reached: sigreturn resumes the interrupted context

caught: db 0

ready_msg:   db "catcher: ready, interrupt me", 10
ready_len    equ $ - ready_msg
caught_msg:  db "catcher: caught SIGINT in the handler", 10
caught_len   equ $ - caught_msg
resumed_msg: db "catcher: resumed after sigreturn", 10
resumed_len  equ $ - resumed_msg
