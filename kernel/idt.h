#ifndef IDT_H
#define IDT_H

#include <stdint.h>

#include "panic.h"      /* struct registers */

typedef void (*irq_handler_fn)(struct registers* r);

void idt_initialize(void);

/* irq is 0-15, not a vector number. */
void irq_install_handler(uint8_t irq, irq_handler_fn handler);

#endif
