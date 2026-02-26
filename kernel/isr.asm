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

; Hardware IRQs, after the PIC is remapped to 0x20. These never carry an error
; code, so they always push the dummy. Same common path as the exceptions -
; the C side tells them apart by vector number.
%macro IRQ 1
global irq%1
irq%1:
    push dword 0
    push dword (32 + %1)
    jmp isr_common
%endmacro

IRQ 0       ; PIT
IRQ 1       ; keyboard
IRQ 2       ; cascade
IRQ 3
IRQ 4
IRQ 5
IRQ 6
IRQ 7
IRQ 8
IRQ 9
IRQ 10
IRQ 11
IRQ 12
IRQ 13
IRQ 14
IRQ 15

; Syscall gate. Same shape as the exceptions that push no error code, so the
; C side sees one consistent frame for everything.
ISR_NOERR 128

; LAPIC vectors: reschedule IPI and the spurious vector.
ISR_NOERR 253
ISR_NOERR 255

; The tail of isr_common, split out because a forked child's first run enters
; here directly: its kernel stack holds a hand-copied registers frame, and
; this path is what unwinds one of those into an iret.
global fork_ret

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

    ; One choke point for signal delivery, whatever door we came in through.
    ; Checks the frame's cs and does nothing for kernel-mode returns. A
    ; forked child enters below at fork_ret and skips this - nothing can be
    ; pending on a process that hasn't run yet.
    extern signal_check
    push esp
    call signal_check
    add esp, 4

    jmp isr_return          ; the normal path holds no run queue lock

; A forked child's first run enters HERE, not at isr_common - its stack was
; hand-built to look like a frame mid-unwind. schedule() handed it the run
; queue lock on the way in, so it has to drop it, which the normal return
; path above must not do.
fork_ret:
    extern thread_release_after_switch
    call thread_release_after_switch

isr_return:
    pop eax                 ; restore data segment
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    popa
    add esp, 8              ; drop vector and error code
    iret
