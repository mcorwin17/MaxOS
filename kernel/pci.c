#include <stdint.h>

#include "pci.h"
#include "io.h"
#include "serial.h"

#define CONFIG_ADDRESS 0xCF8
#define CONFIG_DATA    0xCFC

static uint32_t config_address(uint8_t bus, uint8_t slot, uint8_t func,
                               uint8_t off) {
    /* bit 31 enable, then bus/slot/func/offset. The low two bits of the
     * offset must be zero - config space is dword-addressed. */
    return 0x80000000u
         | ((uint32_t)bus  << 16)
         | ((uint32_t)slot << 11)
         | ((uint32_t)func << 8)
         | (off & 0xFC);
}

uint32_t pci_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off) {
    outl(CONFIG_ADDRESS, config_address(bus, slot, func, off));
    return inl(CONFIG_DATA);
}

void pci_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off,
                 uint32_t value) {
    outl(CONFIG_ADDRESS, config_address(bus, slot, func, off));
    outl(CONFIG_DATA, value);
}

int pci_find(uint16_t vendor, uint16_t device, struct pci_device* out) {
    for (uint32_t slot = 0; slot < 32; ++slot) {
        for (uint32_t func = 0; func < 8; ++func) {
            uint32_t id = pci_read32(0, (uint8_t)slot, (uint8_t)func, 0x00);

            uint16_t v = (uint16_t)(id & 0xFFFF);
            uint16_t d = (uint16_t)(id >> 16);

            if (v == 0xFFFF) continue;      /* nothing there */
            if (v != vendor || d != device) continue;

            out->bus    = 0;
            out->slot   = (uint8_t)slot;
            out->func   = (uint8_t)func;
            out->vendor = v;
            out->device = d;

            for (int i = 0; i < 6; ++i) {
                out->bar[i] = pci_read32(0, (uint8_t)slot, (uint8_t)func,
                                         (uint8_t)(0x10 + i * 4));
            }
            out->irq = (uint8_t)(pci_read32(0, (uint8_t)slot, (uint8_t)func,
                                            0x3C) & 0xFF);
            return 1;
        }
    }
    return 0;
}

void pci_enable(const struct pci_device* dev) {
    uint32_t cmd = pci_read32(dev->bus, dev->slot, dev->func, 0x04);
    cmd |= 0x5;     /* I/O space + bus master */
    pci_write32(dev->bus, dev->slot, dev->func, 0x04, cmd);
}

void pci_dump(void) {
    for (uint32_t slot = 0; slot < 32; ++slot) {
        uint32_t id = pci_read32(0, (uint8_t)slot, 0, 0x00);
        if ((id & 0xFFFF) == 0xFFFF) continue;

        uint32_t class = pci_read32(0, (uint8_t)slot, 0, 0x08);
        kprintf("pci: %u:%u vendor %04x device %04x class %02x%02x\n",
                0, slot, id & 0xFFFF, id >> 16,
                (class >> 24) & 0xFF, (class >> 16) & 0xFF);
    }
}
