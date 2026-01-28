/* FAT16 through the block cache. Reads verified against qemu's vvfat (an
 * implementation that isn't mine); writes verified by parsing the raw image
 * on the host afterwards, and by the file still being there on the next
 * boot. Create, write (with chain growth), truncate and unlink; directory
 * growth is the deliberate gap - roots have hundreds of slots. */

#ifndef FAT16_H
#define FAT16_H

/* Probe the disk and mount at / if there's a FAT filesystem on it, either
 * bare or inside the first MBR partition. Quietly does nothing without a
 * disk. */
void fat16_mount(void);

/* Format the raw disk as FAT16 (destroying whatever's on it) and mount. */
int fat16_format(void);

#endif
