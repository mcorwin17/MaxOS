#include <stdint.h>
#include <stddef.h>

#include "thread.h"
#include "heap.h"
#include "serial.h"
#include "panic.h"
#include "pit.h"
#include "spinlock.h"
#include "console.h"
#include "gdt.h"
#include "vma.h"
#include "process.h"

extern void context_switch(uint32_t* save_esp_here, uint32_t load_esp);
extern void fork_ret(void);     /* isr.asm: pops a registers frame and irets */

static struct thread* all_threads;      /* every thread, in creation order */
static struct thread* current;
static struct thread* idle;
static uint32_t next_id;
static struct spinlock queue_lock;

struct thread* thread_current(void) { return current; }

static void copy_name(char* dst, const char* src) {
    int i = 0;
    while (src && src[i] && i < THREAD_NAME_MAX - 1) { dst[i] = src[i]; ++i; }
    dst[i] = '\0';
}

/* Every thread starts here rather than jumping straight at its entry point,
 * so that returning from the entry function exits cleanly instead of
 * returning into whatever happened to be on a fresh stack. */
static void thread_trampoline(void) {
    struct thread* self = current;

    /* The switch that brought us here ran with interrupts off. */
    __asm__ volatile("sti");

    self->entry(self->arg);
    thread_exit();
}

void thread_initialize(void) {
    spin_init(&queue_lock, "runqueue");

    all_threads = 0;
    next_id     = 0;

    /* The code already running becomes thread 0. It needs a struct so the
     * scheduler has somewhere to save its esp, but not a stack - it's already
     * on one. */
    struct thread* boot = (struct thread*)kmalloc(sizeof(struct thread));
    if (!boot) panic("thread: no memory for the boot thread");

    boot->id        = next_id++;
    boot->state     = THREAD_RUNNING;
    boot->stack     = 0;
    boot->wake_tick = 0;
    boot->ran_ticks = 0;
    boot->entry     = 0;
    boot->arg       = 0;
    boot->proc      = 0;            /* process_initialize fills this in */
    boot->next      = 0;
    copy_name(boot->name, "boot");

    all_threads = boot;
    current     = boot;

    kprintf("thread: boot thread is id %u\n", boot->id);
}

struct thread* thread_create(const char* name, void (*entry)(void*), void* arg) {
    struct thread* t = (struct thread*)kmalloc(sizeof(struct thread));
    if (!t) return 0;

    /* Each thread gets its own stack. Sharing one is an instant and very
     * confusing crash. */
    t->stack = (uint32_t*)kmalloc(THREAD_STACK_SIZE);
    if (!t->stack) { kfree(t); return 0; }

    t->id        = next_id++;
    t->state     = THREAD_READY;
    t->entry     = entry;
    t->arg       = arg;
    t->wake_tick = 0;
    t->ran_ticks = 0;
    t->proc      = current ? current->proc : 0;    /* inherit by default */
    copy_name(t->name, name);

    /* Hand-build a stack that looks exactly like one context_switch just
     * saved, so the first switch into this thread works like every later one.
     * Layout from the top down: return target, then ebx/esi/edi/ebp in the
     * order the pops expect. */
    uint32_t* sp = (uint32_t*)((uint8_t*)t->stack + THREAD_STACK_SIZE);

    *--sp = (uint32_t)(uintptr_t)thread_trampoline;   /* ret lands here */
    *--sp = 0;   /* ebx */
    *--sp = 0;   /* esi */
    *--sp = 0;   /* edi */
    *--sp = 0;   /* ebp */

    t->esp = (uint32_t)(uintptr_t)sp;

    uint32_t flags = spin_lock_irq(&queue_lock);
    t->next = 0;
    struct thread* last = all_threads;
    while (last->next) last = last->next;
    last->next = t;
    spin_unlock_irq(&queue_lock, flags);

    return t;
}

struct thread* thread_create_forked(const char* name,
                                    const struct registers* frame) {
    struct thread* t = (struct thread*)kmalloc(sizeof(struct thread));
    if (!t) return 0;

    t->stack = (uint32_t*)kmalloc(THREAD_STACK_SIZE);
    if (!t->stack) { kfree(t); return 0; }

    t->id        = next_id++;
    t->state     = THREAD_READY;
    t->entry     = 0;
    t->arg       = 0;
    t->wake_tick = 0;
    t->ran_ticks = 0;
    t->proc      = 0;               /* caller assigns the child process */
    copy_name(t->name, name);

    /* Top of the stack gets a copy of the parent's interrupt frame; under
     * it, a context_switch frame whose return address is fork_ret. First
     * switch into this thread pops the callee-saved zeros, rets into
     * fork_ret, which unwinds the interrupt frame and irets straight to
     * user mode. IF is off until the iret restores the parent's eflags, so
     * nothing can land on the crafted frame before it's consumed. */
    uint8_t* top = (uint8_t*)t->stack + THREAD_STACK_SIZE;
    struct registers* child_frame =
        (struct registers*)(top - sizeof(struct registers));
    *child_frame = *frame;

    uint32_t* sp = (uint32_t*)child_frame;
    *--sp = (uint32_t)(uintptr_t)fork_ret;
    *--sp = 0;   /* ebx */
    *--sp = 0;   /* esi */
    *--sp = 0;   /* edi */
    *--sp = 0;   /* ebp */

    t->esp = (uint32_t)(uintptr_t)sp;

    uint32_t flags = spin_lock_irq(&queue_lock);
    t->next = 0;
    struct thread* last = all_threads;
    while (last->next) last = last->next;
    last->next = t;
    spin_unlock_irq(&queue_lock, flags);

    return t;
}

void thread_reap(struct thread* t) {
    if (!t || t == current) panic("thread_reap: bad target");
    if (t->state != THREAD_DEAD) panic("thread_reap: thread isn't dead");

    uint32_t flags = spin_lock_irq(&queue_lock);
    struct thread** link = &all_threads;
    while (*link && *link != t) link = &(*link)->next;
    if (*link) *link = t->next;
    spin_unlock_irq(&queue_lock, flags);

    if (t->stack) kfree(t->stack);
    kfree(t);
}

/* Round robin over the thread list, starting after the current one. */
static struct thread* pick_next(void) {
    struct thread* start = current ? current->next : all_threads;
    if (!start) start = all_threads;

    struct thread* t = start;
    for (uint32_t i = 0; i < 2 * next_id + 2; ++i) {
        if (!t) { t = all_threads; continue; }

        if (t->state == THREAD_READY) return t;

        t = t->next;
    }

    return 0;
}

void schedule(void) {
    uint32_t flags = irq_save();

    struct thread* previous = current;
    struct thread* next = pick_next();

    if (!next || next == previous) {
        /* Nothing else wants the CPU. Keep going. */
        irq_restore(flags);
        return;
    }

    if (previous->state == THREAD_RUNNING) previous->state = THREAD_READY;
    next->state = THREAD_RUNNING;
    current = next;

    /* Interrupts from ring 3 land on esp0. Must be the incoming thread's
     * kernel stack, and must be set before anything can arrive from user
     * mode on this thread. */
    if (next->stack) {
        gdt_set_kernel_stack((uint32_t)(uintptr_t)next->stack
                             + THREAD_STACK_SIZE);
    }

    /* Different process, different page directory. The kernel half of every
     * directory is shared, so the stacks and structs this code stands on
     * don't move when CR3 does. */
    if (next->proc && previous->proc &&
        next->proc->as.pd != previous->proc->as.pd) {
        vma_set_active(&next->proc->as);
        __asm__ volatile("mov %0, %%cr3" : : "r"(next->proc->as.pd));
    }

    context_switch(&previous->esp, next->esp);

    /* Back here when something switches to us again. */
    irq_restore(flags);
}

void thread_yield(void) {
    schedule();
}

void thread_sleep_ms(uint32_t ms) {
    uint32_t flags = irq_save();

    current->wake_tick = pit_ticks() + ((ms * PIT_FREQUENCY_HZ) + 999u) / 1000u;
    current->state     = THREAD_SLEEPING;

    irq_restore(flags);

    /* Leave the run queue until the timer puts us back. Not a busy wait: the
     * idle thread halts when nothing else is runnable. */
    schedule();
}

void thread_exit(void) {
    uint32_t flags = irq_save();
    current->state = THREAD_DEAD;
    irq_restore(flags);

    schedule();

    /* schedule() never comes back to a dead thread. */
    panic("thread_exit: scheduled a dead thread");
}

static volatile int resched_pending;

void thread_tick(uint32_t now) {
    for (struct thread* t = all_threads; t; t = t->next) {
        if (t->state == THREAD_SLEEPING && now >= t->wake_tick) {
            t->state = THREAD_READY;
        }
    }

    if (current) current->ran_ticks++;

    /* Preempt every tick. A longer quantum comes later; round robin at 100 Hz
     * is plenty to prove the machinery works. */
    resched_pending = 1;
}

int thread_take_resched(void) {
    if (!resched_pending) return 0;
    resched_pending = 0;
    return 1;
}

uint32_t thread_count(void) {
    uint32_t n = 0;
    for (struct thread* t = all_threads; t; t = t->next) ++n;
    return n;
}

static const char* state_name(enum thread_state s) {
    switch (s) {
    case THREAD_READY:    return "ready";
    case THREAD_RUNNING:  return "running";
    case THREAD_SLEEPING: return "sleeping";
    case THREAD_WAITING:  return "waiting";
    case THREAD_DEAD:     return "dead";
    }
    return "?";
}

void thread_dump(void) {
    for (struct thread* t = all_threads; t; t = t->next) {
        kprintf("  %u %s %s ticks=%u\n",
                t->id, t->name, state_name(t->state), t->ran_ticks);
    }
}

static void write_padded(const char* s, int width) {
    int n = 0;
    while (s[n]) { console_putchar(s[n]); ++n; }
    while (n < width) { console_putchar(' '); ++n; }
}

static void write_decimal(uint32_t v) {
    char buf[12];
    int i = 0;

    if (v == 0) { console_putchar('0'); return; }
    while (v > 0) { buf[i++] = (char)('0' + (v % 10)); v /= 10; }
    while (i-- > 0) console_putchar(buf[i]);
}

void thread_dump_console(void) {
    for (struct thread* t = all_threads; t; t = t->next) {
        console_write("  ");
        write_decimal(t->id);
        console_write("  ");
        if (t->proc) write_decimal(t->proc->pid); else console_putchar('-');
        console_write("  ");
        write_padded(t->name, THREAD_NAME_MAX);
        write_padded(state_name(t->state), 9);
        write_decimal(t->ran_ticks);
        console_putchar('\n');
    }
}

/* Idle thread. Has to halt rather than spin, or qemu pins a host core and it
 * looks like the kernel is busy when it is doing nothing at all. */
static void idle_entry(void* arg) {
    (void)arg;
    for (;;) {
        __asm__ volatile("hlt");
    }
}

void thread_start_idle(void) {
    idle = thread_create("idle", idle_entry, 0);
    if (!idle) panic("thread: couldn't create the idle thread");
}


/* ---- selftest ---------------------------------------------------------- */

#define WORKERS    4
#define INCREMENTS 200

/* Each worker has to run for several 10 ms ticks or preemption never lands
 * inside a critical section and the unlocked build passes by accident. The
 * first version finished all four workers inside a single tick - ps showed
 * ticks=0 against every one of them, which is what gave it away. */
#define WINDOW_SPINS 20000

static volatile uint32_t shared_counter;
static struct spinlock   counter_lock;
static volatile uint32_t workers_done;

/* Read, pause, write back. The pause is there on purpose: a bare ++ on a
 * volatile is three instructions and a tick almost never lands between them,
 * so an unlocked version would pass by luck and prove nothing. Widening the
 * window makes the race reliable enough to actually demonstrate. */
static void bump_counter(void) {
    uint32_t v = shared_counter;
    for (volatile int d = 0; d < WINDOW_SPINS; ++d) { }
    shared_counter = v + 1;
}

static void counter_worker(void* arg) {
    (void)arg;

    for (uint32_t i = 0; i < INCREMENTS; ++i) {
#ifdef TEST_NO_LOCK
        bump_counter();
#else
        uint32_t flags = spin_lock_irq(&counter_lock);
        bump_counter();
        spin_unlock_irq(&counter_lock, flags);
#endif
    }

    uint32_t flags = spin_lock_irq(&counter_lock);
    workers_done++;
    spin_unlock_irq(&counter_lock, flags);
}

void thread_selftest(void) {
    spin_init(&counter_lock, "counter");
    shared_counter = 0;
    workers_done   = 0;

    for (int i = 0; i < WORKERS; ++i) {
        char name[THREAD_NAME_MAX];
        name[0] = 'w'; name[1] = (char)('0' + i); name[2] = '\0';

        if (!thread_create(name, counter_worker, 0)) {
            panic("thread selftest: couldn't create a worker");
        }
    }

    /* Wait for them by yielding, which also exercises the scheduler from the
     * other side. */
    uint32_t spins = 0;
    while (workers_done < WORKERS) {
        thread_yield();
        if (++spins > 100000000u) panic("thread selftest: workers never finished");
    }

#ifdef TEST_NO_LOCK
    /* Built deliberately without the lock, so a wrong answer is the result.
     * Report rather than panic - the number is the point. */
    kprintf("thread: NO LOCK, counter is %u, expected %u, lost %u increments\n",
            shared_counter, WORKERS * INCREMENTS,
            (WORKERS * INCREMENTS) - shared_counter);
#else
    if (shared_counter != WORKERS * INCREMENTS) {
        kprintf("thread: counter is %u, expected %u\n",
                shared_counter, WORKERS * INCREMENTS);
        panic("thread selftest: lost increments, the lock isn't holding");
    }

    kprintf("thread: selftest ok, %u threads x %u increments = %u\n",
            WORKERS, INCREMENTS, shared_counter);
#endif
}
