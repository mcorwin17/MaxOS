/* Input ring buffer.
 *
 * Both the PS/2 keyboard (IRQ1) and COM1 receive (IRQ4) push into this, so the
 * shell doesn't care where a keystroke came from. That also means the thing is
 * drivable over a serial line, which is the only way to test it without a
 * person sitting at a keyboard. */

#ifndef CONSOLE_H
#define CONSOLE_H

#include <stdint.h>

/* Safe to call from an interrupt handler: no blocking, no allocation.
 * Drops the character if the buffer is full - losing a keystroke is fine,
 * corrupting the buffer is not. */
void console_push(char c);

/* Blocks until there's something to return. Needs interrupts enabled. */
char console_getchar(void);

int console_has_input(void);

/* Output goes to both the screen and the serial line, so a session is
 * readable either way round. */
void console_putchar(char c);
void console_write(const char* str);
void write_decimal_console(uint32_t value);

/* The foreground process is who Ctrl-C is for. While one is set, a 0x03
 * from either input source becomes SIGINT to it instead of a buffered
 * byte. */
struct process;
void console_set_foreground(struct process* p);
void console_clear_foreground(struct process* p);

#endif
