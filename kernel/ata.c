#include <stdint.h>

#include "ata.h"
#include "io.h"
#include "serial.h"
#include "spinlock.h"

#define IO_BASE     0x1F0
#define REG_DATA    (IO_BASE + 0)
#define REG_ERROR   (IO_BASE + 1)
#define REG_COUNT   (IO_BASE + 2)
#define REG_LBA0    (IO_BASE + 3)
#define REG_LBA1    (IO_BASE + 4)
#define REG_LBA2    (IO_BASE + 5)
#define REG_DRIVE   (IO_BASE + 6)
#define REG_STATUS  (IO_BASE + 7)   /* command on write */
#define REG_ALT     0x3F6           /* alt status, no side effects */

#define ST_ERR  0x01
#define ST_DRQ  0x08
#define ST_DF   0x20
#define ST_BSY  0x80

#define CMD_READ     0x20
#define CMD_WRITE    0x30
#define CMD_FLUSH    0xE7
#define CMD_IDENTIFY 0xEC

/* Bounded polling. A missing or wedged drive must give an error, not a
 * hang - every other test in this repo boots without a disk attached. */
#define SPIN_LIMIT  1000000

static int      present;
static uint32_t sectors;
static char     model[41];

static struct spinlock ata_lock;

/* The spec wants ~400ns after a drive select before the status register
 * means anything. Four alt-status reads is the traditional way to buy it. */
static void select_delay(void) {
    inb(REG_ALT); inb(REG_ALT); inb(REG_ALT); inb(REG_ALT);
}

static int wait_not_busy(void) {
    for (uint32_t i = 0; i < SPIN_LIMIT; ++i) {
        if (!(inb(REG_STATUS) & ST_BSY)) return 0;
    }
    return -1;
}

static int wait_data_ready(void) {
    for (uint32_t i = 0; i < SPIN_LIMIT; ++i) {
        uint8_t st = inb(REG_STATUS);
        if (st & (ST_ERR | ST_DF)) return -1;
        if (!(st & ST_BSY) && (st & ST_DRQ)) return 0;
    }
    return -1;
}

static void select_lba(uint32_t lba) {
    outb(REG_DRIVE, (uint8_t)(0xE0 | ((lba >> 24) & 0x0F)));
    select_delay();
    outb(REG_COUNT, 1);
    outb(REG_LBA0, (uint8_t)(lba & 0xFF));
    outb(REG_LBA1, (uint8_t)((lba >> 8) & 0xFF));
    outb(REG_LBA2, (uint8_t)((lba >> 16) & 0xFF));
}

int ata_read_sector(uint32_t lba, void* out512) {
    if (!present || lba >= sectors) return -1;

    uint32_t irq = spin_lock_irq(&ata_lock);

    int rc = -1;
    if (wait_not_busy() == 0) {
        select_lba(lba);
        outb(REG_STATUS, CMD_READ);

        if (wait_data_ready() == 0) {
            uint16_t* out = (uint16_t*)out512;
            for (int i = 0; i < 256; ++i) out[i] = inw(REG_DATA);
            rc = 0;
        }
    }

    spin_unlock_irq(&ata_lock, irq);
    return rc;
}

int ata_write_sector(uint32_t lba, const void* in512) {
    if (!present || lba >= sectors) return -1;

    uint32_t irq = spin_lock_irq(&ata_lock);

    int rc = -1;
    if (wait_not_busy() == 0) {
        select_lba(lba);
        outb(REG_STATUS, CMD_WRITE);

        if (wait_data_ready() == 0) {
            const uint16_t* in = (const uint16_t*)in512;
            for (int i = 0; i < 256; ++i) outw(REG_DATA, in[i]);

            /* Make the device commit before this returns - a write that
             * only reached a drive buffer is a lie waiting for a power
             * cut. */
            outb(REG_STATUS, CMD_FLUSH);
            rc = wait_not_busy();
        }
    }

    spin_unlock_irq(&ata_lock, irq);
    return rc;
}

int ata_present(void)            { return present; }
uint32_t ata_sector_count(void)  { return sectors; }
const char* ata_model(void)      { return model; }

void ata_initialize(void) {
    spin_init(&ata_lock, "ata");
    present = 0;

    /* 0xFF from the status port means nothing is driving the bus. */
    if (inb(REG_STATUS) == 0xFF) {
        kprintf("ata: no controller\n");
        return;
    }

    outb(REG_DRIVE, 0xA0);      /* master, CHS addressing for identify */
    select_delay();
    outb(REG_COUNT, 0);
    outb(REG_LBA0, 0);
    outb(REG_LBA1, 0);
    outb(REG_LBA2, 0);
    outb(REG_STATUS, CMD_IDENTIFY);

    if (inb(REG_STATUS) == 0) {
        kprintf("ata: no drive\n");
        return;
    }

    if (wait_data_ready() != 0) {
        kprintf("ata: identify failed\n");
        return;
    }

    uint16_t id[256];
    for (int i = 0; i < 256; ++i) id[i] = inw(REG_DATA);

    /* Words 60-61: LBA28 sector count. Words 27-46: model, byte-swapped
     * per word because ATA strings predate agreeing on byte order. */
    sectors = ((uint32_t)id[61] << 16) | id[60];

    for (int i = 0; i < 20; ++i) {
        model[i * 2]     = (char)(id[27 + i] >> 8);
        model[i * 2 + 1] = (char)(id[27 + i] & 0xFF);
    }
    model[40] = '\0';
    for (int i = 39; i >= 0 && (model[i] == ' ' || model[i] == '\0'); --i) {
        model[i] = '\0';
    }

    present = 1;
    kprintf("ata: %s, %u sectors (%u MB)\n",
            model, sectors, sectors / 2048);
}
