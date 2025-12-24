/* System calls.
 *
 * int 0x80, number in eax, arguments in ebx/ecx/edx, result back in eax.
 * The gate is a trap gate (0xEF), not an interrupt gate: interrupts stay on
 * during a syscall, so a slow one can be preempted like any kernel code.
 *
 * Why int 0x80 and not syscall/sysret: that advice is for x86-64, where
 * SYSCALL is the mechanism. On 32-bit, AMD's syscall barely exists and
 * sysenter is a fast path worth adding later, not a starting point. */

#ifndef SYSCALL_H
#define SYSCALL_H

#include <stdint.h>
#include "panic.h"

#define SYS_EXIT      1
#define SYS_FORK      2
#define SYS_WRITE     3
#define SYS_GETPID    4
#define SYS_SLEEP     5
#define SYS_YIELD     6
#define SYS_WAIT      7
#define SYS_EXEC      8
#define SYS_SIGNAL    9     /* (sig, handler, restorer) */
#define SYS_KILL      10    /* (pid, sig) */
#define SYS_SIGRETURN 11

/* Called from isr_handler when the vector is 0x80. Reads the arguments out
 * of the saved frame and writes the return value into r->eax, which popa
 * hands back to the user. */
void syscall_dispatch(struct registers* r);

#endif
