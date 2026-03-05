/* NE2000 (RTL8029AS as qemu presents it).
 *
 * Chosen over virtio-net deliberately: virtio is the better device and the
 * right answer for throughput, but it's descriptor rings and feature
 * negotiation before a single byte moves. The NE2000 is port I/O and a
 * 16 KB on-card buffer - the whole driver fits in one readable file, which
 * is what a first NIC should be.
 *
 * Receive is polled from the network thread rather than IRQ-driven. The
 * card's IRQ is fine; polling just keeps the packet path off the interrupt
 * stack while the stack is this young. */

#ifndef NE2000_H
#define NE2000_H

#include <stdint.h>

#define ETH_ALEN     6
#define ETH_MTU      1500
#define ETH_FRAME_MAX 1518

int  ne2000_initialize(void);
int  ne2000_present(void);

const uint8_t* ne2000_mac(void);

/* Non-zero on failure. Blocks only as long as the card needs. */
int  ne2000_send(const void* frame, uint32_t len);

/* Returns bytes received, 0 if nothing is waiting. */
uint32_t ne2000_receive(void* out, uint32_t max);

#endif
