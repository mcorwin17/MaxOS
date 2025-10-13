#ifndef PANIC_H
#define PANIC_H

#include <stdint.h>

/* What the ISR stubs push. Order is the reverse of how they push it, so this
 * has to stay in step with isr.asm. */
struct registers {
    uint32_t ds;
    uint32_t edi, esi, ebp, esp_dummy, ebx, edx, ecx, eax;
    uint32_t vector, error_code;
    uint32_t eip, cs, eflags, useresp, ss;   /* pushed by the CPU */
};

void panic(const char* fmt, ...) __attribute__((noreturn));
void panic_with_frame(struct registers* r, const char* message) __attribute__((noreturn));
void backtrace(uint32_t ebp);

#define KASSERT(cond)                                                    \
    do {                                                                 \
        if (!(cond)) {                                                   \
            panic("assertion failed: %s\n  at %s:%d",                    \
                  #cond, __FILE__, __LINE__);                            \
        }                                                                \
    } while (0)

#endif
