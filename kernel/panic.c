#include <stdint.h>

#include "panic.h"
#include "serial.h"

/* Stack lives just under 0x90000 and grows down; the kernel sits at 0x1000.
 * Anything outside that is not a frame pointer, it's garbage, and following
 * it turns a useful dump into a hang. */
#define STACK_TOP     0x90000
#define STACK_FLOOR   0x1000
#define MAX_FRAMES    16

static void halt_forever(void) __attribute__((noreturn));

static void halt_forever(void) {
    for (;;) {
        __asm__ volatile("cli; hlt");
    }
    __builtin_unreachable();
}

void backtrace(uint32_t ebp) {
    kprintf("backtrace:\n");

    for (int depth = 0; depth < MAX_FRAMES; ++depth) {
        if (ebp < STACK_FLOOR || ebp >= STACK_TOP || (ebp & 0x3)) {
            break;
        }

        uint32_t* frame = (uint32_t*)ebp;
        uint32_t  next  = frame[0];
        uint32_t  ret   = frame[1];

        if (ret == 0) break;

        kprintf("  [%d] 0x%08x\n", depth, ret);

        if (next <= ebp) break;    /* frames go up, or we're chasing our tail */
        ebp = next;
    }
}

void panic(const char* fmt, ...) {
    uint32_t ebp;
    __asm__ volatile("mov %%ebp, %0" : "=r"(ebp));

    kprintf("\n*** PANIC ***\n");

    /* Can't forward varargs to kprintf without a vkprintf, and a panic path
     * that needs more machinery is a panic path that can itself fail. Print
     * the format string's plain text and let the caller pass the detail in
     * separate calls if it matters. */
    serial_write(fmt);
    serial_write("\n");

    backtrace(ebp);

    kprintf("halted.\n");
    halt_forever();
}

void panic_with_frame(struct registers* r, const char* message) {
    kprintf("\n*** PANIC ***\n%s\n", message);

    kprintf("vector %u  error 0x%08x\n", r->vector, r->error_code);
    kprintf("eip 0x%08x  cs 0x%04x  eflags 0x%08x\n",
            r->eip, r->cs, r->eflags);
    kprintf("eax 0x%08x  ebx 0x%08x  ecx 0x%08x  edx 0x%08x\n",
            r->eax, r->ebx, r->ecx, r->edx);
    kprintf("esi 0x%08x  edi 0x%08x  ebp 0x%08x  ds  0x%04x\n",
            r->esi, r->edi, r->ebp, r->ds);

    uint32_t cr2;
    __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
    if (r->vector == 14) {
        kprintf("cr2 0x%08x  (faulting address)\n", cr2);
    }

    backtrace(r->ebp);

    kprintf("halted.\n");
    halt_forever();
}
