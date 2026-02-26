/* Symmetric multiprocessing.
 *
 * The MADT says which CPUs exist, INIT-SIPI-SIPI wakes them through the
 * trampoline, and each one ends up in ap_main with its own idle thread.
 * Kernel threads roam every CPU; user processes stay pinned to the boot
 * CPU until there's a TSS per CPU to catch their ring transitions.
 *
 * Preemption on APs: the BSP's PIT tick broadcasts a reschedule IPI. The
 * legacy PIC only talks to the boot CPU, so this is what "the timer" means
 * everywhere else. */

#ifndef SMP_H
#define SMP_H

#include <stdint.h>

#define MAX_CPUS         8
#define IPI_RESCHED_VEC  0xFD
#define LAPIC_SPURIOUS_VEC 0xFF

struct thread;

struct cpu {
    uint32_t        lapic_id;
    int             online;
    struct thread*  current;
    struct thread*  idle;
    uint32_t        sched_count;    /* switches taken here, for the demo */
};

extern struct cpu cpus[MAX_CPUS];

/* Which CPU am I? 0 until the LAPIC is mapped, which is true exactly when
 * only CPU 0 exists. */
uint32_t this_cpu(void);

uint32_t smp_cpu_count(void);

/* Find CPUs, start them. Call late: threads must already work, and the
 * calling CPU busy-waits on the PIT during bring-up. */
void smp_initialize(void);

/* EOI for LAPIC-delivered vectors (IPIs). PIC vectors keep their own. */
void lapic_eoi(void);

/* Kick every other CPU into schedule(). No-op before APs exist. */
void lapic_broadcast_resched(void);

/* Spawns worker threads and checks they land on more than one CPU, and that
 * a locked counter survives genuinely parallel contention. */
void smp_selftest(void);

#endif
