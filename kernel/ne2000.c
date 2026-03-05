#include <stdint.h>

#include "ne2000.h"
#include "pci.h"
#include "io.h"
#include "serial.h"
#include "spinlock.h"

/* Page 0 registers, offsets from the I/O base. */
#define CR        0x00      /* command, all pages */
#define CLDA0     0x01
#define BNRY      0x03      /* boundary: last page the host has read */
#define TSR       0x04
#define TPSR      0x04      /* (write) transmit page start */
#define TBCR0     0x05
#define TBCR1     0x06
#define ISR       0x07      /* interrupt status */
#define RSAR0     0x08      /* remote DMA address */
#define RSAR1     0x09
#define RBCR0     0x0A      /* remote DMA byte count */
#define RBCR1     0x0B
#define RCR       0x0C      /* receive config */
#define TCR       0x0D      /* transmit config */
#define DCR       0x0E      /* data config */
#define IMR       0x0F      /* interrupt mask */

/* Page 0 read-only */
#define BOUNDARY  0x03
#define CURR_P1   0x07      /* page 1: current receive page */

#define PSTART    0x01      /* page 1 alias of receive ring start */
#define PSTOP     0x02
#define PAR0      0x01      /* page 1: MAC address */

#define DATA      0x10      /* remote DMA data window */
#define RESET     0x1F

/* Commands */
#define CR_STOP   0x01
#define CR_START  0x02
#define CR_TXP    0x04
#define CR_RD_READ  0x08
#define CR_RD_WRITE 0x10
#define CR_RD_ABORT 0x20
#define CR_PAGE0  0x00
#define CR_PAGE1  0x40

/* On-card buffer layout, in 256-byte pages. The card has 16 KB starting at
 * 0x4000 in its own address space; pages here are that space / 256. */
#define TX_PAGE_START 0x40
#define RX_PAGE_START 0x46
#define RX_PAGE_STOP  0x80

static uint16_t io_base;
static int      present;
static uint8_t  mac[ETH_ALEN];
static uint8_t  next_packet;        /* our read pointer into the RX ring */
static struct spinlock nic_lock;

int ne2000_present(void)        { return present; }
const uint8_t* ne2000_mac(void) { return mac; }

/* Remote DMA: the only way to reach the card's buffer memory. Set address
 * and count, then stream through the data port. */
static void dma_read(uint16_t offset, void* dst, uint16_t len) {
    outb(io_base + CR, CR_PAGE0 | CR_RD_ABORT | CR_START);
    outb(io_base + RBCR0, (uint8_t)(len & 0xFF));
    outb(io_base + RBCR1, (uint8_t)(len >> 8));
    outb(io_base + RSAR0, (uint8_t)(offset & 0xFF));
    outb(io_base + RSAR1, (uint8_t)(offset >> 8));
    outb(io_base + CR, CR_PAGE0 | CR_RD_READ | CR_START);

    uint16_t* out = (uint16_t*)dst;
    for (uint16_t i = 0; i < (len + 1) / 2; ++i) out[i] = inw(io_base + DATA);
}

static void dma_write(uint16_t offset, const void* src, uint16_t len) {
    outb(io_base + CR, CR_PAGE0 | CR_RD_ABORT | CR_START);
    outb(io_base + ISR, 0x40);                  /* clear remote DMA complete */
    outb(io_base + RBCR0, (uint8_t)(len & 0xFF));
    outb(io_base + RBCR1, (uint8_t)(len >> 8));
    outb(io_base + RSAR0, (uint8_t)(offset & 0xFF));
    outb(io_base + RSAR1, (uint8_t)(offset >> 8));
    outb(io_base + CR, CR_PAGE0 | CR_RD_WRITE | CR_START);

    const uint16_t* in = (const uint16_t*)src;
    for (uint16_t i = 0; i < (len + 1) / 2; ++i) outw(io_base + DATA, in[i]);

    for (uint32_t spin = 0; spin < 100000; ++spin) {
        if (inb(io_base + ISR) & 0x40) break;
    }
}

int ne2000_initialize(void) {
    spin_init(&nic_lock, "ne2000");
    present = 0;

    struct pci_device dev;
    /* Realtek 8029, which is what qemu's -net nic,model=ne2k_pci reports. */
    if (!pci_find(0x10EC, 0x8029, &dev)) {
        kprintf("ne2000: no card\n");
        return -1;
    }

    pci_enable(&dev);
    io_base = (uint16_t)(dev.bar[0] & ~0x3u);

    kprintf("ne2000: found at pci %u:%u, io 0x%04x, irq %u\n",
            dev.bus, dev.slot, io_base, dev.irq);

    /* Reset: reading the reset port and writing it back is the documented
     * kick, then wait for ISR bit 7. */
    outb(io_base + RESET, inb(io_base + RESET));
    for (uint32_t spin = 0; spin < 100000; ++spin) {
        if (inb(io_base + ISR) & 0x80) break;
    }
    outb(io_base + ISR, 0xFF);

    outb(io_base + CR,  CR_PAGE0 | CR_RD_ABORT | CR_STOP);
    outb(io_base + DCR, 0x49);      /* word transfers, normal, 8-byte FIFO */
    outb(io_base + RBCR0, 0);
    outb(io_base + RBCR1, 0);
    outb(io_base + RCR, 0x20);      /* monitor mode while we set up */
    outb(io_base + TCR, 0x02);      /* internal loopback while we set up */

    /* The MAC lives in the first 12 bytes of card memory as byte-per-word;
     * read 12 and take every other one. */
    uint8_t prom[16];
    dma_read(0, prom, 16);
    for (int i = 0; i < ETH_ALEN; ++i) mac[i] = prom[i * 2];

    /* Receive ring. */
    outb(io_base + TPSR,   TX_PAGE_START);
    outb(io_base + PSTART, RX_PAGE_START);
    outb(io_base + BNRY,   RX_PAGE_START);
    outb(io_base + PSTOP,  RX_PAGE_STOP);

    /* Page 1: our MAC into the address filter, and the current page. */
    outb(io_base + CR, CR_PAGE1 | CR_RD_ABORT | CR_STOP);
    for (int i = 0; i < ETH_ALEN; ++i) outb(io_base + PAR0 + i, mac[i]);
    outb(io_base + CURR_P1, RX_PAGE_START + 1);
    next_packet = RX_PAGE_START + 1;

    outb(io_base + CR, CR_PAGE0 | CR_RD_ABORT | CR_START);
    outb(io_base + ISR, 0xFF);
    outb(io_base + IMR, 0x00);      /* polled, so mask everything */
    outb(io_base + TCR, 0x00);      /* normal transmit */
    outb(io_base + RCR, 0x04);      /* accept broadcast + matching unicast */

    present = 1;
    kprintf("ne2000: mac %02x:%02x:%02x:%02x:%02x:%02x\n",
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return 0;
}

int ne2000_send(const void* frame, uint32_t len) {
    if (!present) return -1;
    if (len > ETH_FRAME_MAX) return -1;

    /* Ethernet's 60-byte minimum, padding included. */
    if (len < 60) len = 60;

    uint32_t irq = spin_lock_irq(&nic_lock);

    dma_write(TX_PAGE_START << 8, frame, (uint16_t)len);

    outb(io_base + TPSR,  TX_PAGE_START);
    outb(io_base + TBCR0, (uint8_t)(len & 0xFF));
    outb(io_base + TBCR1, (uint8_t)(len >> 8));
    outb(io_base + CR, CR_PAGE0 | CR_RD_ABORT | CR_TXP | CR_START);

    spin_unlock_irq(&nic_lock, irq);
    return 0;
}

uint32_t ne2000_receive(void* out, uint32_t max) {
    if (!present) return 0;

    uint32_t irq = spin_lock_irq(&nic_lock);

    /* CURR is where the card will write next; when it equals our read
     * pointer the ring is empty. */
    outb(io_base + CR, CR_PAGE1 | CR_RD_ABORT | CR_START);
    uint8_t current = inb(io_base + CURR_P1);
    outb(io_base + CR, CR_PAGE0 | CR_RD_ABORT | CR_START);

    if (current == next_packet) {
        spin_unlock_irq(&nic_lock, irq);
        return 0;
    }

    /* Every packet starts with a 4-byte header the card writes: status,
     * next page, and the length including the header itself. */
    struct { uint8_t status, next, len_lo, len_hi; } hdr;
    dma_read((uint16_t)next_packet << 8, &hdr, sizeof(hdr));

    uint32_t len = ((uint32_t)hdr.len_hi << 8 | hdr.len_lo);
    if (len < 4 + 14 || len > ETH_FRAME_MAX + 4 ||
        hdr.next < RX_PAGE_START || hdr.next >= RX_PAGE_STOP) {
        /* Ring desynced - the honest move is to resynchronise on the
         * card's own pointer rather than keep walking garbage. */
        kprintf("ne2000: bad rx header, resyncing\n");
        next_packet = current;
        outb(io_base + BNRY,
             (uint8_t)(current == RX_PAGE_START ? RX_PAGE_STOP - 1
                                                : current - 1));
        spin_unlock_irq(&nic_lock, irq);
        return 0;
    }

    len -= 4;
    if (len > max) len = max;

    /* The payload may wrap the ring; dma_read handles a contiguous run, so
     * split at the ring end. */
    uint32_t start = ((uint32_t)next_packet << 8) + 4;
    uint32_t ring_end = (uint32_t)RX_PAGE_STOP << 8;

    if (start + len <= ring_end) {
        dma_read((uint16_t)start, out, (uint16_t)len);
    } else {
        uint32_t first = ring_end - start;
        dma_read((uint16_t)start, out, (uint16_t)first);
        dma_read((uint16_t)RX_PAGE_START << 8,
                 (uint8_t*)out + first, (uint16_t)(len - first));
    }

    next_packet = hdr.next;
    outb(io_base + BNRY,
         (uint8_t)(hdr.next == RX_PAGE_START ? RX_PAGE_STOP - 1
                                             : hdr.next - 1));

    spin_unlock_irq(&nic_lock, irq);
    return len;
}
