/* Spinlocks, and the interrupt-disable helpers that go with them.
 *
 * Anything an interrupt handler touches has to be taken with interrupts off.
 * A timer tick in the middle of a run queue manipulation corrupts the list,
 * and the resulting crash lands nowhere near the code that caused it. */

#ifndef SPINLOCK_H
#define SPINLOCK_H

#include <stdint.h>

struct spinlock {
    volatile uint32_t locked;
    const char*       name;
};

static inline uint32_t xchg(volatile uint32_t* addr, uint32_t value) {
    __asm__ volatile("xchgl %0, %1"
                     : "+m"(*addr), "+r"(value)
                     :
                     : "memory");
    return value;
}

/* Returns the previous interrupt state so it can be put back exactly as it
 * was. Blindly doing sti on release re-enables interrupts in code that had
 * deliberately turned them off. */
static inline uint32_t irq_save(void) {
    uint32_t flags;
    __asm__ volatile("pushfl; popl %0; cli" : "=r"(flags) : : "memory");
    return flags;
}

static inline void irq_restore(uint32_t flags) {
    __asm__ volatile("pushl %0; popfl" : : "r"(flags) : "memory", "cc");
}

static inline void spin_init(struct spinlock* lock, const char* name) {
    lock->locked = 0;
    lock->name   = name;
}

static inline void spin_lock(struct spinlock* lock) {
    while (xchg(&lock->locked, 1) != 0) {
        __asm__ volatile("pause");
    }
}

static inline void spin_unlock(struct spinlock* lock) {
    xchg(&lock->locked, 0);
}

/* The variant to use for anything an IRQ handler can also touch. */
static inline uint32_t spin_lock_irq(struct spinlock* lock) {
    uint32_t flags = irq_save();
    spin_lock(lock);
    return flags;
}

static inline void spin_unlock_irq(struct spinlock* lock, uint32_t flags) {
    spin_unlock(lock);
    irq_restore(flags);
}

#endif
