#include <stdint.h>

#include "pic.h"
#include "io.h"

#define PIC1_COMMAND  0x20
#define PIC1_DATA     0x21
#define PIC2_COMMAND  0xA0
#define PIC2_DATA     0xA1

#define ICW1_INIT     0x11      /* init, expect ICW4 */
#define ICW4_8086     0x01
#define OCW3_READ_ISR 0x0B
#define EOI           0x20

void pic_remap(void) {
    uint8_t mask1 = inb(PIC1_DATA);
    uint8_t mask2 = inb(PIC2_DATA);

    outb(PIC1_COMMAND, ICW1_INIT);           io_wait();
    outb(PIC2_COMMAND, ICW1_INIT);           io_wait();

    outb(PIC1_DATA, PIC_VECTOR_BASE);        io_wait();   /* IRQ 0-7  -> 32-39 */
    outb(PIC2_DATA, PIC_VECTOR_BASE + 8);    io_wait();   /* IRQ 8-15 -> 40-47 */

    outb(PIC1_DATA, 0x04);                   io_wait();   /* slave on IRQ2 */
    outb(PIC2_DATA, 0x02);                   io_wait();   /* slave identity */

    outb(PIC1_DATA, ICW4_8086);              io_wait();
    outb(PIC2_DATA, ICW4_8086);              io_wait();

    outb(PIC1_DATA, mask1);
    outb(PIC2_DATA, mask2);
}

void pic_send_eoi(uint8_t irq) {
    /* Anything on the slave has to be acknowledged to both. Forgetting the
     * master here means that IRQ fires exactly once and then never again. */
    if (irq >= 8) {
        outb(PIC2_COMMAND, EOI);
    }
    outb(PIC1_COMMAND, EOI);
}

void pic_mask(uint8_t irq) {
    uint16_t port = (irq < 8) ? PIC1_DATA : PIC2_DATA;
    if (irq >= 8) irq -= 8;

    outb(port, inb(port) | (uint8_t)(1 << irq));
}

void pic_unmask(uint8_t irq) {
    uint16_t port = (irq < 8) ? PIC1_DATA : PIC2_DATA;
    if (irq >= 8) irq -= 8;

    outb(port, inb(port) & (uint8_t)~(1 << irq));
}

int pic_is_spurious(uint8_t irq) {
    if (irq != 7 && irq != 15) return 0;

    uint16_t command = (irq == 7) ? PIC1_COMMAND : PIC2_COMMAND;
    uint8_t  bit     = (irq == 7) ? 0x80 : 0x80;

    outb(command, OCW3_READ_ISR);
    if (inb(command) & bit) return 0;    /* really in service, not spurious */

    /* Spurious on the slave still needs the master acknowledged, because the
     * master did see a real cascade request on IRQ2. */
    if (irq == 15) outb(PIC1_COMMAND, EOI);

    return 1;
}
