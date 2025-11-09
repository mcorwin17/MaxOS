; MaxOS boot sector
; Loads the kernel off the floppy, switches to protected mode, jumps to it.

[org 0x7C00]
[bits 16]

KERNEL_OFFSET        equ 0x1000
KERNEL_SECTOR_COUNT  equ 48        ; 24 KB, ends at 0x7000
KERNEL_START_LBA     equ 1         ; LBA 0 is this sector
STACK_TOP            equ 0x9000
DISK_RETRY_COUNT     equ 3

; 1.44M floppy geometry, for the LBA to CHS conversion.
SECTORS_PER_TRACK    equ 18
HEADS                equ 2

; E820 memory map, left where the kernel can pick it up. Below the kernel on
; purpose: it used to sit at 0x8000 and the kernel grew into it.
; 0x500 is the first byte after the BIOS data area.
E820_COUNT           equ 0x500     ; dword: how many entries follow
E820_ENTRIES         equ 0x504     ; 24 bytes each
E820_MAX_ENTRIES     equ 32        ; 768 bytes, so the map ends by 0x804

; Entry has to be the first thing in the file. nasm -f bin emits in assembly
; order and the BIOS jumps straight to 0x7C00, so the includes go at the bottom.
_start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, STACK_TOP
    sti

    mov [boot_drive], dl        ; BIOS hands us the drive number here

    mov ax, 0x0003              ; 80x25 text
    int 0x10

    mov si, msg_boot
    call print_string

    ; Has to happen here. int 0x15 is a BIOS call and the BIOS is gone the
    ; moment we're in protected mode, so the kernel can never ask for itself.
    call detect_memory

    call load_kernel
    jc  disk_error

    mov si, msg_ok
    call print_string

    cli
    call switch_to_protected_mode
    jmp $                       ; not reached


; Read the kernel to 0x0000:0x1000. CF set on failure.
;
; One sector per int 13h call. Asking for 48 in a single call is the obvious
; thing and it's wrong: a track is only 18 sectors, and a BIOS is not required
; to read across a track boundary. QEMU's does, which is exactly why this
; survived so long - it would have failed on the first real machine.
load_kernel:
    mov si, msg_load
    call print_string

    xor ax, ax
    mov es, ax
    mov bx, KERNEL_OFFSET       ; ES:BX destination, walks forward
    mov si, KERNEL_START_LBA    ; current LBA
    mov di, KERNEL_SECTOR_COUNT ; sectors left

.sector:
    mov byte [retries], DISK_RETRY_COUNT

.attempt:
    ; LBA -> CHS.  sector = lba % spt + 1, head = (lba / spt) % heads,
    ; cylinder = lba / (spt * heads)
    ;
    ; Divisor goes in bp, not cx: cl holds the sector number from the first
    ; divide and a second `mov cx, ...` would quietly wipe it.
    mov ax, si
    xor dx, dx
    mov bp, SECTORS_PER_TRACK
    div bp                      ; ax = lba/spt, dx = lba%spt
    inc dl
    mov cl, dl                  ; sector, 1 based

    xor dx, dx
    mov bp, HEADS
    div bp                      ; ax = cylinder, dx = head
    mov ch, al
    mov dh, dl
    mov dl, [boot_drive]

    mov ah, 0x02
    mov al, 1
    int 0x13
    jnc .advance

    ; Reset the controller and try again. Floppies fail spuriously and one
    ; attempt is not enough.
    xor ah, ah
    mov dl, [boot_drive]
    int 0x13

    dec byte [retries]
    jnz .attempt

    stc
    ret

.advance:
    inc si
    add bx, 512                 ; 48 sectors keeps this under 64K, no wrap
    dec di
    jnz .sector

    clc
    ret


; Walk the BIOS memory map into E820_ENTRIES, count into E820_COUNT.
; Writes 0 for the count if the BIOS won't play, which the kernel treats as
; "no map" rather than "no memory".
detect_memory:
    pusha
    xor ax, ax
    mov es, ax
    mov di, E820_ENTRIES
    xor ebx, ebx                ; continuation, 0 on the first call
    xor bp, bp                  ; entries kept
    mov edx, 0x534D4150         ; 'SMAP'

.next:
    mov eax, 0xE820
    mov ecx, 24
    ; Some BIOSes ignore the ACPI 3 extended attribute word. Pre-set the
    ; "valid" bit so a short reply still looks sane.
    mov dword [es:di + 20], 1
    int 0x15
    jc .done                    ; CF on the first call means unsupported
    cmp eax, 0x534D4150
    jne .done

    ; Drop zero-length regions by reusing the slot rather than advancing.
    mov ecx, [es:di + 8]
    or  ecx, [es:di + 12]
    jz  .skip

    inc bp
    cmp bp, E820_MAX_ENTRIES
    jae .done
    add di, 24

.skip:
    test ebx, ebx               ; 0 means that was the last one
    jnz .next

.done:
    movzx eax, bp
    mov [E820_COUNT], eax
    popa
    ret


disk_error:
    mov si, msg_err
    call print_string
    cli
.hang:
    hlt
    jmp .hang


boot_drive: db 0
retries:    db 0

; keep these short, they compete with code for 512 bytes
msg_boot:   db "MaxOS", 0x0D, 0x0A, 0
msg_load:   db "Loading kernel...", 0x0D, 0x0A, 0
msg_ok:     db "OK", 0x0D, 0x0A, 0
msg_err:    db "Disk read failed", 0x0D, 0x0A, 0


; Order matters: switchpm.asm ends in [bits 32], so only 32-bit code after it.
%include "bootloader/pstring.asm"
%include "bootloader/gdt.asm"
%include "bootloader/switchpm.asm"
%include "bootloader/ppstring.asm"

%if ($ - $$) > 510
  %error "boot sector too big, needs a second stage"
%endif

times 510 - ($ - $$) db 0x00
dw 0xAA55
