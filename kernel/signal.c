#include <stdint.h>

#include "signal.h"
#include "process.h"
#include "thread.h"
#include "vma.h"
#include "serial.h"
#include "spinlock.h"

void signal_send(struct process* p, int sig) {
    if (!p || sig <= 0 || sig >= NSIG) return;

    uint32_t irq = irq_save();

    p->sig_pending |= (1u << sig);

    /* Wake the target so it can reach the delivery point. A SLEEPING thread
     * returns from its sleep early - harmless, only timing. */
    struct thread* t = p->thread;
    if (t && (t->state == THREAD_SLEEPING || t->state == THREAD_WAITING)) {
        t->state = THREAD_READY;
    }

    irq_restore(irq);
}

int signal_install(struct process* p, int sig, uint32_t handler,
                   uint32_t restorer) {
    if (sig <= 0 || sig >= NSIG) return -1;
    if (sig == SIGKILL) return -1;      /* the point of SIGKILL */

    p->sig_handler[sig] = handler;
    p->sig_restorer     = restorer;
    return 0;
}

void signal_check(struct registers* r) {
    /* Only on the way down to ring 3. Kernel-mode returns fall through in
     * two compares. */
    if ((r->cs & 3) != 3) return;

    struct process* p = process_current();
    if (!p || !p->sig_pending || p->sig_in_handler) return;

    /* Lowest pending signal wins. */
    int sig = 0;
    for (int i = 1; i < NSIG; ++i) {
        if (p->sig_pending & (1u << i)) { sig = i; break; }
    }
    if (!sig) return;

    p->sig_pending &= ~(1u << sig);

    uint32_t handler = p->sig_handler[sig];
    if (sig == SIGKILL) handler = SIG_DFL;      /* uncatchable */

    if (handler == SIG_IGN) return;

    if (handler == SIG_DFL) {
        if (sig == SIGCHLD) return;             /* default is ignore */

        kprintf("process %u killed by signal %d\n", p->pid, sig);
        process_exit(128 + sig);
    }

    /* Hand the frame to the handler. The interrupted context parks in the
     * process struct; the handler returns through the restorer stub, whose
     * sigreturn puts it back.
     *
     * User stack gets [restorer][signum] - cdecl entry for handler(sig).
     * Vet the range first: if their esp is garbage, writing through it
     * would be a KERNEL fault on user memory, and that must not panic. */
    uint32_t sp = r->useresp - 8;

    if (!vma_user_range_ok(vma_active(), sp, 8)) {
        kprintf("process %u: signal %d with a bad stack, killing\n",
                p->pid, sig);
        process_exit(128 + sig);
    }

    p->sig_saved      = *r;
    p->sig_in_handler = 1;

    uint32_t* user_sp = (uint32_t*)sp;
    user_sp[0] = p->sig_restorer;       /* handler's return address */
    user_sp[1] = (uint32_t)sig;         /* its argument */

    r->useresp = sp;
    r->eip     = handler;
}

int signal_return(struct registers* r) {
    struct process* p = process_current();
    if (!p || !p->sig_in_handler) return -1;

    /* The whole frame comes back, eax included - sigreturn doesn't have a
     * return value, it has a restored world. */
    *r = p->sig_saved;
    p->sig_in_handler = 0;
    return 0;
}
