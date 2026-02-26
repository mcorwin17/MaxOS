#include <stdint.h>

#include "smp.h"
#include "serial.h"
#include "panic.h"
#include "vmm.h"
#include "pmm.h"
#include "heap.h"
#include "thread.h"
#include "idt.h"
#include "pit.h"
#include "spinlock.h"

/* ---- ACPI tables --------------------------------------------------------- */

struct rsdp {
    char     sig[8];        /* "RSD PTR " */
    uint8_t  checksum;
    char     oemid[6];
    uint8_t  revision;
    uint32_t rsdt;
} __attribute__((packed));

struct sdt_header {
    char     sig[4];
    uint32_t length;
    uint8_t  revision;
    uint8_t  checksum;
    char     oemid[6];
    char     oemtableid[8];
    uint32_t oemrevision;
    uint32_t creatorid;
    uint32_t creatorrevision;
} __attribute__((packed));

/* MADT entry types. */
#define MADT_LAPIC   0
#define MADT_IOAPIC  1

/* ---- LAPIC --------------------------------------------------------------- */

#define LAPIC_ID        0x020
#define LAPIC_EOI       0x0B0
#define LAPIC_SPURIOUS  0x0F0
#define LAPIC_ICR_LOW   0x300
#define LAPIC_ICR_HIGH  0x310

#define ICR_INIT        0x00000500
#define ICR_STARTUP     0x00000600
#define ICR_ASSERT      0x00004000
#define ICR_LEVEL       0x00008000

static volatile uint8_t* lapic;     /* identity-mapped MMIO */
static uint32_t          lapic_phys;

struct cpu cpus[MAX_CPUS];
static uint32_t cpu_count;
static uint32_t bsp_lapic_id;

extern char trampoline_start[];     /* not used; kept for symmetry */
extern void ap_main(void);          /* below */

static uint32_t lapic_read(uint32_t reg) {
    return *(volatile uint32_t*)(lapic + reg);
}

static void lapic_write(uint32_t reg, uint32_t value) {
    *(volatile uint32_t*)(lapic + reg) = value;
}

void lapic_eoi(void) {
    if (lapic) lapic_write(LAPIC_EOI, 0);
}

uint32_t this_cpu(void) {
    if (!lapic) return 0;

    uint32_t id = lapic_read(LAPIC_ID) >> 24;
    for (uint32_t i = 0; i < cpu_count; ++i) {
        if (cpus[i].lapic_id == id) return i;
    }
    return 0;
}

uint32_t smp_cpu_count(void) {
    return cpu_count ? cpu_count : 1;
}

/* ---- table walking ------------------------------------------------------- */

static int checksum_ok(const void* p, uint32_t len) {
    const uint8_t* b = (const uint8_t*)p;
    uint8_t sum = 0;
    for (uint32_t i = 0; i < len; ++i) sum += b[i];
    return sum == 0;
}

static int sig_is(const char* got, const char* want, uint32_t n) {
    for (uint32_t i = 0; i < n; ++i) {
        if (got[i] != want[i]) return 0;
    }
    return 1;
}

/* Anything a table points at has to live in RAM we can actually read. The
 * whole reason this function exists: an unvalidated RSDT walk found garbage
 * in the BIOS ROM, reported "1 CPU", and set lapic_phys to 0xf000fe95 - so
 * the first LAPIC read faulted at 0xf000feb5. Signatures and checksums are
 * how ACPI expects to be told apart from coincidence. */
static int in_ram(uint32_t addr, uint32_t len) {
    uint64_t end = (uint64_t)addr + len;
    return addr >= 0x1000 && end <= (uint64_t)pmm_map_limit();
}

static const struct sdt_header* checked_sdt(uint32_t addr, const char* sig) {
    if (!in_ram(addr, sizeof(struct sdt_header))) return 0;

    const struct sdt_header* h = (const struct sdt_header*)(uintptr_t)addr;
    if (h->length < sizeof(struct sdt_header) || h->length > 0x10000) return 0;
    if (!in_ram(addr, h->length)) return 0;
    if (sig && !sig_is(h->sig, sig, 4)) return 0;
    if (!checksum_ok(h, h->length)) return 0;

    return h;
}

static const struct rsdp* check_rsdp_at(uint32_t a) {
    const struct rsdp* r = (const struct rsdp*)(uintptr_t)a;

    if (!sig_is(r->sig, "RSD PTR ", 8)) return 0;
    if (!checksum_ok(r, 20)) return 0;

    /* The pointer has to lead somewhere that really is an RSDT. */
    if (!checked_sdt(r->rsdt, "RSDT")) return 0;

    return r;
}

static const struct rsdp* find_rsdp(void) {
    /* The EBDA first (its segment lives at 0x40E), then the BIOS ROM area.
     * 16-byte boundaries, per spec. */
    uint32_t ebda = (uint32_t)(*(const uint16_t*)(uintptr_t)0x40E) << 4;

    if (ebda >= 0x400 && ebda < 0xA0000) {
        for (uint32_t a = ebda; a < ebda + 1024; a += 16) {
            const struct rsdp* r = check_rsdp_at(a);
            if (r) return r;
        }
    }

    for (uint32_t a = 0x000E0000; a < 0x00100000; a += 16) {
        const struct rsdp* r = check_rsdp_at(a);
        if (r) return r;
    }

    return 0;
}

static const struct sdt_header* find_madt(const struct rsdp* rsdp) {
    const struct sdt_header* rsdt = checked_sdt(rsdp->rsdt, "RSDT");
    if (!rsdt) return 0;

    uint32_t entries = (rsdt->length - sizeof(struct sdt_header)) / 4;
    const uint32_t* table =
        (const uint32_t*)((const uint8_t*)rsdt + sizeof(struct sdt_header));

    for (uint32_t i = 0; i < entries; ++i) {
        const struct sdt_header* h = checked_sdt(table[i], "APIC");
        if (h) return h;
    }
    return 0;
}

static void parse_madt(const struct sdt_header* madt) {
    const uint8_t* p = (const uint8_t*)madt + sizeof(struct sdt_header);

    lapic_phys = *(const uint32_t*)p;            /* local APIC address */
    p += 8;                                      /* skip addr + flags */

    const uint8_t* end = (const uint8_t*)madt + madt->length;

    while (p < end) {
        uint8_t type = p[0];
        uint8_t len  = p[1];
        if (len == 0) break;

        if (type == MADT_LAPIC) {
            uint8_t  apic_id = p[3];
            uint32_t flags   = *(const uint32_t*)(p + 4);

            /* bit 0 = enabled, bit 1 = online-capable. Either can be woken.
             *
             * Identity fields only. cpus[0].current already holds the boot
             * thread - thread_initialize ran long before this - and zeroing
             * it here left the scheduler reading a null current on the very
             * next tick. */
            if ((flags & 0x3) && cpu_count < MAX_CPUS) {
                cpus[cpu_count].lapic_id = apic_id;
                cpus[cpu_count].online   = 0;
                cpu_count++;
            }
        }

        p += len;
    }
}

/* ---- AP bring-up --------------------------------------------------------- */

#define TRAMP_ADDR   0x8000
#define TRAMP_MAGIC  0x54524D50u

extern uint8_t tramp_blob_start[];
extern uint8_t tramp_blob_end[];

/* Located by scanning the copied trampoline for its magic, so the asm layout
 * can change without touching offsets here. */
static volatile uint32_t* tramp_field(uint32_t magic_off_index) {
    volatile uint32_t* base = (volatile uint32_t*)TRAMP_ADDR;
    uint32_t words = 0x1000 / 4;

    for (uint32_t i = 0; i < words; ++i) {
        if (base[i] == TRAMP_MAGIC) {
            return &base[i + magic_off_index];
        }
    }
    panic("smp: trampoline magic vanished");
    return 0;
}

/* Crude spin delay; the exact duration doesn't matter, only that it's
 * "long enough" for INIT and SIPI to settle. */
static void spin(volatile uint32_t n) {
    while (n--) __asm__ volatile("pause");
}

static volatile int ap_ready;

static void start_ap(uint32_t idx) {
    uint32_t apic_id = cpus[idx].lapic_id;

    /* Each AP gets a real kernel stack and its own idle thread. */
    uint32_t stack = (uint32_t)(uintptr_t)kmalloc(16 * 1024);
    if (!stack) panic("smp: no stack for an AP");

    *tramp_field(1) = vmm_kernel_directory();        /* cr3 */
    *tramp_field(2) = stack + 16 * 1024;             /* esp (top) */
    *tramp_field(3) = (uint32_t)(uintptr_t)ap_main;  /* entry */

    ap_ready = 0;

    /* INIT, then two SIPIs at vector 0x08 (0x8000 >> 12). The double SIPI is
     * the AMD-recommended dance; real hardware sometimes drops the first. */
    lapic_write(LAPIC_ICR_HIGH, apic_id << 24);
    lapic_write(LAPIC_ICR_LOW, ICR_INIT | ICR_ASSERT | ICR_LEVEL);
    spin(100000);

    for (int s = 0; s < 2; ++s) {
        lapic_write(LAPIC_ICR_HIGH, apic_id << 24);
        lapic_write(LAPIC_ICR_LOW, ICR_STARTUP | ICR_ASSERT | 0x08);
        spin(100000);
    }

    /* Wait for the AP to raise the flag, but don't hang the whole kernel if
     * it never shows. */
    for (uint32_t t = 0; t < 100 && !ap_ready; ++t) spin(200000);

    if (ap_ready) {
        cpus[idx].online = 1;
        kprintf("smp: CPU %u (lapic %u) online\n", idx, apic_id);
    } else {
        kprintf("smp: CPU %u (lapic %u) didn't start\n", idx, apic_id);
    }
}

/* First C the AP runs, on its trampoline stack. */
void ap_main(void) {
    uint32_t id = this_cpu();

    idt_load_on_this_cpu();
    lapic_write(LAPIC_SPURIOUS, 0x100 | LAPIC_SPURIOUS_VEC);  /* enable */

    ap_ready = 1;

    /* Become this CPU's idle thread and start scheduling. From here the AP
     * is a peer: any runnable kernel thread can land on it. */
    thread_become_idle_ap(id);

    __asm__ volatile("sti");
    for (;;) {
        schedule();
        __asm__ volatile("hlt");
    }
}

void lapic_broadcast_resched(void) {
    if (!lapic || cpu_count < 2) return;

    /* All-excluding-self shorthand (bits 19:18 = 11). */
    lapic_write(LAPIC_ICR_HIGH, 0);
    lapic_write(LAPIC_ICR_LOW, 0x000C0000 | IPI_RESCHED_VEC);
}

/* ---- selftest ------------------------------------------------------------ */

#define SMP_WORKERS 6
#define SMP_ROUNDS  400

static volatile uint32_t smp_counter;
static struct spinlock   smp_lock;
static volatile uint32_t smp_done;
static volatile uint32_t seen_on_cpu[MAX_CPUS];

static void smp_worker(void* arg) {
    (void)arg;

    for (uint32_t i = 0; i < SMP_ROUNDS; ++i) {
        /* Record where this iteration ran. On one CPU this is always 0;
         * with APs it fans out, and that fanning IS the test. */
        seen_on_cpu[this_cpu()]++;

        uint32_t f = spin_lock_irq(&smp_lock);
        uint32_t v = smp_counter;
        for (volatile int d = 0; d < 300; ++d) { }
        smp_counter = v + 1;
        spin_unlock_irq(&smp_lock, f);
    }

    uint32_t f = spin_lock_irq(&smp_lock);
    smp_done++;
    spin_unlock_irq(&smp_lock, f);
}

void smp_selftest(void) {
    spin_init(&smp_lock, "smptest");
    smp_counter = 0;
    smp_done    = 0;
    for (uint32_t i = 0; i < MAX_CPUS; ++i) seen_on_cpu[i] = 0;

    for (int i = 0; i < SMP_WORKERS; ++i) {
        char name[8];
        name[0] = 's'; name[1] = (char)('0' + i); name[2] = '\0';
        if (!thread_create(name, smp_worker, 0)) panic("smp selftest: no thread");
    }

    while (smp_done < SMP_WORKERS) thread_yield();

    if (smp_counter != SMP_WORKERS * SMP_ROUNDS) {
        kprintf("smp: counter is %u, expected %u\n",
                smp_counter, SMP_WORKERS * SMP_ROUNDS);
        panic("smp selftest: lost increments under real parallelism");
    }

    uint32_t used = 0;
    for (uint32_t i = 0; i < cpu_count; ++i) {
        if (seen_on_cpu[i]) used++;
        kprintf("smp:   cpu %u ran %u iterations, %u switches\n",
                i, seen_on_cpu[i], cpus[i].sched_count);
    }

    kprintf("smp: selftest ok, %u x %u = %u across %u CPUs\n",
            SMP_WORKERS, SMP_ROUNDS, smp_counter, used);

    if (cpu_count > 1 && used < 2) {
        panic("smp selftest: every thread stayed on one CPU");
    }
}

void smp_initialize(void) {
    const struct rsdp* rsdp = find_rsdp();
    if (!rsdp) { kprintf("smp: no RSDP, staying uniprocessor\n"); return; }

    const struct sdt_header* madt = find_madt(rsdp);
    if (!madt) {
        kprintf("smp: no MADT, staying uniprocessor\n");
        return;
    }

    parse_madt(madt);
    if (cpu_count == 0) { kprintf("smp: MADT lists no CPUs?\n"); return; }

    /* MMIO, so it must be ABOVE RAM - a "LAPIC address" pointing into RAM
     * means the table was misread, and reading it would fault or, worse,
     * quietly return whatever bytes live there. */
    if (lapic_phys < pmm_map_limit() || (lapic_phys & 0xFFF)) {
        kprintf("smp: LAPIC address 0x%08x is not plausible MMIO, "
                "staying uniprocessor\n", lapic_phys);
        cpu_count = 0;
        return;
    }

    /* Map it. Above RAM, so the identity map of physical memory doesn't
     * already cover it. */
    lapic = (volatile uint8_t*)(uintptr_t)lapic_phys;
    vmm_map(lapic_phys, lapic_phys, PAGE_WRITE);

    /* Every process directory needs this slot too. this_cpu() reads the
     * LAPIC, this_cpu() is called from schedule(), and schedule() runs on
     * whatever CR3 happens to be loaded - including a user process's. Miss
     * this and the kernel faults the instant userspace is scheduled. */
    vmm_share_pde(lapic_phys);

    bsp_lapic_id = lapic_read(LAPIC_ID) >> 24;
    lapic_write(LAPIC_SPURIOUS, 0x100 | LAPIC_SPURIOUS_VEC);

    kprintf("smp: %u CPUs in the MADT, lapic at 0x%08x, BSP is lapic %u\n",
            cpu_count, lapic_phys, bsp_lapic_id);

    if (cpu_count == 1) return;

    /* Copy the trampoline where a SIPI can reach it. */
    uint32_t len = (uint32_t)(tramp_blob_end - tramp_blob_start);
    uint8_t* dst = (uint8_t*)TRAMP_ADDR;
    for (uint32_t i = 0; i < len; ++i) dst[i] = tramp_blob_start[i];

    /* CPU 0 is us; wake the rest one at a time. */
    for (uint32_t i = 0; i < cpu_count; ++i) {
        if (cpus[i].lapic_id == bsp_lapic_id) { cpus[i].online = 1; continue; }
        start_ap(i);
    }
}
