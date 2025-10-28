/* COM1. The only output that leaves the VM, so everything worth checking
 * goes out here rather than to the screen. */

#ifndef SERIAL_H
#define SERIAL_H

void serial_initialize(void);
void serial_write_char(char c);
void serial_write(const char* str);

/* Turn on receive interrupts (IRQ4) so typing into the serial port reaches
 * the console buffer. Call after the IDT and PIC are up. */
void serial_enable_input(void);

/* Understands %s %c %d %u %x %p %%, and a zero-pad width for %x (%08x). */
void kprintf(const char* fmt, ...);

#endif
