/* 8259 PIC.
 *
 * It powers up mapping IRQ0 onto vector 8, which is also the double fault
 * vector, so a timer tick is indistinguishable from the CPU falling over.
 * Remap before enabling anything. */

#ifndef PIC_H
#define PIC_H

#include <stdint.h>

#define PIC_VECTOR_BASE   0x20      /* IRQ0 -> vector 32 */

void pic_remap(void);
void pic_send_eoi(uint8_t irq);
void pic_mask(uint8_t irq);
void pic_unmask(uint8_t irq);

/* IRQ 7 and 15 fire spuriously. Those need no EOI to the master, and
 * acknowledging one that never happened loses a real interrupt later. */
int pic_is_spurious(uint8_t irq);

#endif
