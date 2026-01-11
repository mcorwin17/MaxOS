/* FAT16, read-only, through the block cache.
 *
 * Read-only is a scope choice, not a shortcut being hidden: the roadmap's
 * order is read support fully working first, write second. The test
 * filesystem comes from qemu's vvfat driver, which synthesizes real FAT
 * from a host directory - so this reader is verified against an
 * implementation that isn't mine. */

#ifndef FAT16_H
#define FAT16_H

/* Probe the disk and mount at / if there's a FAT filesystem on it, either
 * bare or inside the first MBR partition. Quietly does nothing without a
 * disk. */
void fat16_mount(void);

#endif
