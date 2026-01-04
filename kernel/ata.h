/* ATA PIO, primary channel, master drive, LBA28.
 *
 * Polling, no IRQ14 - at one request at a time there's nothing useful to do
 * while waiting, and polling keeps the driver readable. Interrupts earn
 * their place when there's a scheduler-visible reason to sleep here.
 *
 * The roadmap said virtio, but that advice was written for the 64-bit cloud
 * path. On a legacy BIOS design, ATA PIO is the native choice, and it's what
 * qemu's default IDE controller speaks. */

#ifndef ATA_H
#define ATA_H

#include <stdint.h>

#define ATA_SECTOR_SIZE 512

/* Probe and identify. Fine to call when no disk is attached - everything
 * else then reports absent instead of hanging. */
void ata_initialize(void);

int  ata_present(void);
uint32_t ata_sector_count(void);
const char* ata_model(void);

/* One sector at a time. Non-zero on error or no drive. */
int ata_read_sector(uint32_t lba, void* out512);
int ata_write_sector(uint32_t lba, const void* in512);

#endif
