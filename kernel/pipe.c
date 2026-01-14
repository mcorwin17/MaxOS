#include <stdint.h>

#include "pipe.h"
#include "heap.h"
#include "thread.h"
#include "spinlock.h"
#include "signal.h"
#include "process.h"

#define SIGPIPE 13

/* One lock for all pipes: contention is two processes and a shell. */
static struct spinlock pipe_lock;
static int lock_ready;

static void ensure_lock(void) {
    if (!lock_ready) { spin_init(&pipe_lock, "pipe"); lock_ready = 1; }
}

struct pipe* pipe_create(void) {
    ensure_lock();

    struct pipe* p = (struct pipe*)kmalloc(sizeof(struct pipe));
    if (!p) return 0;

    p->rpos = p->count = 0;
    p->readers = 1;
    p->writers = 1;
    return p;
}

void pipe_ref_reader(struct pipe* p)  { uint32_t f = spin_lock_irq(&pipe_lock); p->readers++; spin_unlock_irq(&pipe_lock, f); }
void pipe_ref_writer(struct pipe* p)  { uint32_t f = spin_lock_irq(&pipe_lock); p->writers++; spin_unlock_irq(&pipe_lock, f); }

/* Once both counts are zero nothing can reach the pipe again, so freeing
 * outside the lock is safe - there's no one left to race with. */
void pipe_close_reader(struct pipe* p) {
    uint32_t f = spin_lock_irq(&pipe_lock);
    p->readers--;
    int gone = (p->readers == 0 && p->writers == 0);
    spin_unlock_irq(&pipe_lock, f);
    if (gone) kfree(p);
}

void pipe_close_writer(struct pipe* p) {
    uint32_t f = spin_lock_irq(&pipe_lock);
    p->writers--;
    int gone = (p->readers == 0 && p->writers == 0);
    spin_unlock_irq(&pipe_lock, f);
    if (gone) kfree(p);
}

/* Yield-polling rather than wait queues: correct, simple, and a little
 * wasteful. Wait queues earn their place when pipes carry real load; today
 * they carry one pipeline demo. */
int pipe_read(struct pipe* p, void* buf, uint32_t n) {
    uint8_t* out = (uint8_t*)buf;
    if (n == 0) return 0;

    for (;;) {
        uint32_t f = spin_lock_irq(&pipe_lock);

        if (p->count > 0) {
            uint32_t got = 0;
            while (got < n && p->count > 0) {
                out[got++] = p->data[p->rpos];
                p->rpos = (p->rpos + 1) % PIPE_BUF;
                p->count--;
            }
            spin_unlock_irq(&pipe_lock, f);
            return (int)got;
        }

        int writers = (int)p->writers;
        spin_unlock_irq(&pipe_lock, f);

        if (writers == 0) return 0;     /* drained and closed: EOF */

        thread_yield();
    }
}

int pipe_write(struct pipe* p, const void* buf, uint32_t n) {
    const uint8_t* in = (const uint8_t*)buf;
    uint32_t sent = 0;

    while (sent < n) {
        uint32_t f = spin_lock_irq(&pipe_lock);

        if (p->readers == 0) {
            spin_unlock_irq(&pipe_lock, f);
            /* Nobody will ever read this. Unix says SIGPIPE, and the
             * default action kills the writer - which is what makes
             * `yes | head` terminate instead of running forever. */
            signal_send(process_current(), SIGPIPE);
            return -1;
        }

        while (sent < n && p->count < PIPE_BUF) {
            p->data[(p->rpos + p->count) % PIPE_BUF] = in[sent++];
            p->count++;
        }

        spin_unlock_irq(&pipe_lock, f);

        if (sent < n) thread_yield();   /* full: wait for the reader */
    }

    return (int)sent;
}
