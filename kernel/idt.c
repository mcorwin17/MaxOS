#include <stdint.h>

#include "idt.h"
#include "panic.h"
#include "serial.h"
#include "pic.h"
#include "vma.h"

#define IDT_ENTRIES     256
#define KERNEL_CODE_SEG 0x08

/* present, ring 0, 32-bit interrupt gate */
#define GATE_FLAGS      0x8E

struct idt_entry {
    uint16_t base_low;
    uint16_t selector;
    uint8_t  always_zero;
    uint8_t  flags;
    uint16_t base_high;
} __attribute__((packed));

struct idt_pointer {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

static struct idt_entry   idt[IDT_ENTRIES];
static struct idt_pointer idtp;

/* From isr.asm. */
extern void isr0(void);  extern void isr1(void);  extern void isr2(void);
extern void isr3(void);  extern void isr4(void);  extern void isr5(void);
extern void isr6(void);  extern void isr7(void);  extern void isr8(void);
extern void isr9(void);  extern void isr10(void); extern void isr11(void);
extern void isr12(void); extern void isr13(void); extern void isr14(void);
extern void isr15(void); extern void isr16(void); extern void isr17(void);
extern void isr18(void); extern void isr19(void); extern void isr20(void);
extern void isr21(void); extern void isr22(void); extern void isr23(void);
extern void isr24(void); extern void isr25(void); extern void isr26(void);
extern void isr27(void); extern void isr28(void); extern void isr29(void);
extern void isr30(void); extern void isr31(void);

static void (*const exception_stubs[32])(void) = {
    isr0,  isr1,  isr2,  isr3,  isr4,  isr5,  isr6,  isr7,
    isr8,  isr9,  isr10, isr11, isr12, isr13, isr14, isr15,
    isr16, isr17, isr18, isr19, isr20, isr21, isr22, isr23,
    isr24, isr25, isr26, isr27, isr28, isr29, isr30, isr31
};

extern void irq0(void);  extern void irq1(void);  extern void irq2(void);
extern void irq3(void);  extern void irq4(void);  extern void irq5(void);
extern void irq6(void);  extern void irq7(void);  extern void irq8(void);
extern void irq9(void);  extern void irq10(void); extern void irq11(void);
extern void irq12(void); extern void irq13(void); extern void irq14(void);
extern void irq15(void);

static void (*const irq_stubs[16])(void) = {
    irq0,  irq1,  irq2,  irq3,  irq4,  irq5,  irq6,  irq7,
    irq8,  irq9,  irq10, irq11, irq12, irq13, irq14, irq15
};

static irq_handler_fn irq_handlers[16] = { 0 };

static const char* const exception_names[32] = {
    "divide error",
    "debug",
    "non-maskable interrupt",
    "breakpoint",
    "overflow",
    "bound range exceeded",
    "invalid opcode",
    "device not available",
    "double fault",
    "coprocessor segment overrun",
    "invalid TSS",
    "segment not present",
    "stack segment fault",
    "general protection fault",
    "page fault",
    "reserved",
    "x87 floating point",
    "alignment check",
    "machine check",
    "SIMD floating point",
    "virtualisation",
    "control protection",
    "reserved", "reserved", "reserved", "reserved",
    "reserved", "reserved", "reserved",
    "VMM communication",
    "security exception",
    "reserved"
};

static void idt_set_gate(int n, uint32_t handler) {
    idt[n].base_low    = (uint16_t)(handler & 0xFFFF);
    idt[n].base_high   = (uint16_t)((handler >> 16) & 0xFFFF);
    idt[n].selector    = KERNEL_CODE_SEG;
    idt[n].always_zero = 0;
    idt[n].flags       = GATE_FLAGS;
}

void idt_initialize(void) {
    idtp.limit = (uint16_t)(sizeof(idt) - 1);   /* limit is size-1 */
    idtp.base  = (uint32_t)(uintptr_t)&idt;

    for (int i = 0; i < IDT_ENTRIES; ++i) {
        idt_set_gate(i, 0);
        idt[i].flags = 0;                       /* not present */
    }

    for (int i = 0; i < 32; ++i) {
        idt_set_gate(i, (uint32_t)(uintptr_t)exception_stubs[i]);
    }

    /* Remap before installing the IRQ gates, so that if something fires early
     * it lands on a vector we own rather than on top of an exception. */
    pic_remap();

    for (int i = 0; i < 16; ++i) {
        idt_set_gate(PIC_VECTOR_BASE + i, (uint32_t)(uintptr_t)irq_stubs[i]);
    }

    __asm__ volatile("lidt %0" : : "m"(idtp));
}

void irq_install_handler(uint8_t irq, irq_handler_fn handler) {
    if (irq < 16) irq_handlers[irq] = handler;
}

/* Decode the page fault error code, since it's the one worth reading and the
 * bits are impossible to remember. */
static void describe_page_fault(uint32_t error_code) {
    kprintf("  %s, on a %s, from %s\n",
            (error_code & 0x1) ? "protection violation" : "page not present",
            (error_code & 0x2) ? "write" : "read",
            (error_code & 0x4) ? "user mode" : "kernel mode");

    if (error_code & 0x10) kprintf("  during an instruction fetch\n");
}

/* Called from isr_common in isr.asm, for both exceptions and IRQs. */
void isr_handler(struct registers* r) {
    if (r->vector >= PIC_VECTOR_BASE && r->vector < PIC_VECTOR_BASE + 16) {
        uint8_t irq = (uint8_t)(r->vector - PIC_VECTOR_BASE);

        /* Check before doing anything else: acknowledging an interrupt that
         * never happened swallows a real one later. */
        if (pic_is_spurious(irq)) return;

        if (irq_handlers[irq]) irq_handlers[irq](r);

        pic_send_eoi(irq);
        return;
    }

    /* A page fault inside a reserved region isn't an error, it's the region
     * being used for the first time. Service it and return without a word. */
    if (r->vector == 14) {
        uint32_t cr2;
        __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));

        if (vma_handle_fault(cr2)) return;
    }

    const char* name = (r->vector < 32) ? exception_names[r->vector]
                                        : "unknown";

    kprintf("\n=== exception %u: %s ===\n", r->vector, name);

    if (r->vector == 14) {
        describe_page_fault(r->error_code);
    }

    panic_with_frame(r, name);
}
