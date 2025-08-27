; --------------------------------------------------
; Function: print_string
; Description: Prints a null-terminated string to screen in real mode
; Assumes: SI points to the string
; --------------------------------------------------

print_string:
    pusha                   ; Save all registers
    mov ah, 0x0e           ; BIOS teletype function
.loop:
    lodsb                   ; Load character from SI into AL, increment SI
    test al, al            ; Check if character is null (end of string)
    jz .done               ; If null, we're done
    int 0x10               ; Print character using BIOS
    jmp .loop              ; Continue with next character
.done:
    popa                    ; Restore all registers
    ret
