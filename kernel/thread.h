/* Threads and the scheduler.
 *
 * Kernel threads only - one address space, no user mode yet. Cooperative
 * yield works on its own; preemption is the same scheduler driven from the
 * timer. */

#ifndef THREAD_H
#define THREAD_H

#include <stdint.h>
#include "panic.h"      /* struct registers, for thread_create_forked */

#define THREAD_NAME_MAX   16
#define THREAD_STACK_SIZE 8192

struct process;

enum thread_state {
    THREAD_READY,
    THREAD_RUNNING,
    THREAD_SLEEPING,    /* timer wakes it at wake_tick */
    THREAD_WAITING,     /* something else wakes it explicitly */
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

    struct process* proc;   /* owning process; kernel threads use process 0 */

    /* SMP. on_cpu is the claim: a thread only runs on one CPU at a time,
     * and the run queue is shared, so READY alone isn't enough to stop two
     * CPUs picking the same thread. pinned_cpu is -1 for kernel threads
     * (they roam) and 0 for user threads (one TSS, one esp0). */
    int on_cpu;
    int pinned_cpu;

    char name[THREAD_NAME_MAX];
    struct thread* next;    /* all threads, not just runnable */
};

void thread_initialize(void);

struct thread* thread_create(const char* name, void (*entry)(void*), void* arg);

/* A thread whose first run doesn't start at an entry function but pops a
 * saved interrupt frame and irets - which is how a forked child resumes in
 * user mode exactly where its parent was. The frame is copied verbatim, so
 * set the child's eax before calling. */
struct thread* thread_create_forked(const char* name,
                                    const struct registers* frame);

/* Unlink a dead thread and free its stack and struct. Never the current
 * thread - something has to be standing on solid ground to do the freeing,
 * which is the whole reason zombies exist. */
void thread_reap(struct thread* t);

/* Hand the CPU on voluntarily. */
void thread_yield(void);

/* Block until the tick count gets there. Needs interrupts on. */
void thread_sleep_ms(uint32_t ms);

void thread_exit(void) __attribute__((noreturn));

struct thread* thread_current(void);
uint32_t thread_count(void);

void thread_start_idle(void);

/* An AP adopts its trampoline stack as a thread struct so the scheduler has
 * somewhere to save its esp, then becomes that CPU's idle thread. */
void thread_become_idle_ap(uint32_t cpu);

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
