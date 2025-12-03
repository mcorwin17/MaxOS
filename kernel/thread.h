/* Threads and the scheduler.
 *
 * Kernel threads only - one address space, no user mode yet. Cooperative
 * yield works on its own; preemption is the same scheduler driven from the
 * timer. */

#ifndef THREAD_H
#define THREAD_H

#include <stdint.h>

#define THREAD_NAME_MAX   16
#define THREAD_STACK_SIZE 8192

enum thread_state {
    THREAD_READY,
    THREAD_RUNNING,
    THREAD_SLEEPING,
    THREAD_DEAD
};

struct thread {
    /* esp must stay first: switch.asm writes through the pointer it's given
     * and this is what that pointer points at. */
    uint32_t esp;

    uint32_t id;
    enum thread_state state;

    void   (*entry)(void*);
    void*    arg;

    uint32_t* stack;        /* allocation base, for freeing */
    uint32_t  wake_tick;    /* when state is SLEEPING */
    uint32_t  ran_ticks;    /* rough CPU accounting */

    char name[THREAD_NAME_MAX];
    struct thread* next;    /* all threads, not just runnable */
};

void thread_initialize(void);

struct thread* thread_create(const char* name, void (*entry)(void*), void* arg);

/* Hand the CPU on voluntarily. */
void thread_yield(void);

/* Block until the tick count gets there. Needs interrupts on. */
void thread_sleep_ms(uint32_t ms);

void thread_exit(void) __attribute__((noreturn));

struct thread* thread_current(void);
uint32_t thread_count(void);

void thread_start_idle(void);

/* Called from the timer IRQ. Wakes anything whose deadline passed and flags
 * that the running thread should be preempted. */
void thread_tick(uint32_t now);

/* The IRQ path must not switch mid-handler: EOI hasn't been sent, so the PIC
 * would deliver nothing further until this thread ran again. Set a flag here
 * and let isr_handler reschedule once it's acknowledged the interrupt. */
int thread_take_resched(void);

/* Runs the scheduler. Safe to call from thread context; the IRQ path defers
 * to it after sending EOI. */
void schedule(void);

void thread_dump(void);          /* to serial, for boot logs */
void thread_dump_console(void);  /* to screen and serial, for the shell */
void thread_selftest(void);

#endif
