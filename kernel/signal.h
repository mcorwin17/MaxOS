/* Signals.
 *
 * A pending bitmask and a handler table per process. Delivery happens at one
 * choke point: the return to ring 3, whichever door the kernel was entered
 * through - syscall, IRQ or fault. That's what makes Ctrl-C able to
 * interrupt a sleep, a busy loop, or anything between.
 *
 * One signal in flight at a time: the interrupted context is parked in the
 * process struct and delivery is blocked until sigreturn puts it back. Fine
 * while the signal sources are a keyboard and exits; revisit if that grows. */

#ifndef SIGNAL_H
#define SIGNAL_H

#include <stdint.h>
#include "panic.h"

#define SIGINT   2
#define SIGKILL  9
#define SIGSEGV  11
#define SIGCHLD  17
#define NSIG     32

#define SIG_DFL  0u
#define SIG_IGN  1u

struct process;

/* Safe from IRQ context: sets a bit, maybe flips a thread state. */
void signal_send(struct process* p, int sig);

/* signal() syscall: install a handler and the shared restorer stub the
 * handler returns through. SIGKILL refuses. */
int  signal_install(struct process* p, int sig, uint32_t handler,
                    uint32_t restorer);

/* Called on every return to ring 3 (from isr_common). Default action is
 * exit(128+sig), Unix style; a handler gets the frame redirected at it. */
void signal_check(struct registers* r);

/* sigreturn syscall: un-park the interrupted context. */
int  signal_return(struct registers* r);

#endif
