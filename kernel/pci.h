/* PCI configuration space, the 0xCF8/0xCFC port pair.
 *
 * Just enough to find a device by vendor/device id and read its BARs and
 * interrupt line. No bridges, no capability walking - a brute-force scan of
 * bus 0 finds everything qemu puts on the board. */

#ifndef PCI_H
#define PCI_H

#include <stdint.h>

struct pci_device {
    uint8_t  bus, slot, func;
    uint16_t vendor, device;
    uint32_t bar[6];
    uint8_t  irq;
};

uint32_t pci_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off);
void     pci_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off,
                     uint32_t value);

/* Non-zero if found. */
int pci_find(uint16_t vendor, uint16_t device, struct pci_device* out);

/* Set the bus-master and I/O-space enable bits in the command register. */
void pci_enable(const struct pci_device* dev);

void pci_dump(void);

#endif
