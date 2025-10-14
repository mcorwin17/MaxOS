; Exception stubs for vectors 0-31.
;
; The annoying bit: some exceptions push an error code and some don't, so the
; ones that don't push a dummy zero. Without that the stack frame is a
; different shape depending on which fault fired, and every register the C
; handler reads is off by four bytes.
;
; Pushing error codes: 8, 10, 11, 12, 13, 14, 17, 21, 29, 30.

[bits 32]

extern isr_handler

%macro ISR_NOERR 1
global isr%1
isr%1:
    push dword 0            ; dummy, to match the ones that do push
    push dword %1
    jmp isr_common
%endmacro

%macro ISR_ERR 1
global isr%1
isr%1:
    ; CPU already pushed the error code
    push dword %1
    jmp isr_common
%endmacro

ISR_NOERR 0     ; divide error
ISR_NOERR 1     ; debug
ISR_NOERR 2     ; NMI
ISR_NOERR 3     ; breakpoint
ISR_NOERR 4     ; overflow
ISR_NOERR 5     ; bound range
ISR_NOERR 6     ; invalid opcode
ISR_NOERR 7     ; device not available
ISR_ERR   8     ; double fault
ISR_NOERR 9     ; coprocessor segment overrun
ISR_ERR   10    ; invalid TSS
ISR_ERR   11    ; segment not present
ISR_ERR   12    ; stack segment fault
ISR_ERR   13    ; general protection fault
ISR_ERR   14    ; page fault
ISR_NOERR 15    ; reserved
ISR_NOERR 16    ; x87 floating point
ISR_ERR   17    ; alignment check
ISR_NOERR 18    ; machine check
ISR_NOERR 19    ; SIMD floating point
ISR_NOERR 20    ; virtualisation
ISR_ERR   21    ; control protection
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_ERR   29    ; VMM communication
ISR_ERR   30    ; security exception
ISR_NOERR 31    ; reserved

; By the time we reach the C handler the stack looks exactly like
; struct registers in panic.h. Keep the two in step.
isr_common:
    pusha                   ; eax ecx edx ebx esp ebp esi edi
    mov ax, ds
    push eax                ; save the caller's data segment

    mov ax, 0x10            ; kernel data selector
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    push esp                ; -> struct registers
    call isr_handler
    add esp, 4

    pop eax                 ; restore data segment
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    popa
    add esp, 8              ; drop vector and error code
    iret
