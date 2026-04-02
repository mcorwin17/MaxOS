/* Ethernet / ARP / IPv4 / ICMP.
 *
 * Static address, no routing table, no fragmentation. TCP sits on top of
 * this in tcp.c rather than in here - the split is the point, IP has no
 * opinion about what protocol 6 means.
 *
 * Everything on the wire is big-endian, so every multi-byte field crosses
 * a byte-swap on the way in and out. Getting that wrong is the classic way
 * to spend an afternoon staring at a checksum. */

#ifndef NET_H
#define NET_H

#include <stdint.h>

#define IP_ADDR(a,b,c,d) (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | \
                          ((uint32_t)(c) << 8)  |  (uint32_t)(d))

/* qemu's user-mode network: guest 10.0.2.15, gateway/host 10.0.2.2. */
#define NET_OUR_IP     IP_ADDR(10, 0, 2, 15)
#define NET_GATEWAY_IP IP_ADDR(10, 0, 2, 2)

void net_initialize(void);
int  net_up(void);

/* Change our address. Flushes the ARP cache - the old mappings were made
 * on behalf of an identity we no longer have. */
void     net_set_ip(uint32_t ip);
uint32_t net_our_ip(void);

/* Wrap a payload in an IPv4 header and put it on the wire. Public because
 * TCP lives in its own file - IP doesn't need to know what protocol 6 is,
 * and TCP doesn't need to know what a MAC address is. */
void net_send_ip(uint32_t dst_ip, uint8_t protocol,
                 const void* payload, uint32_t len);

/* Feed one received ethernet frame to the stack. */
void net_input(const uint8_t* frame, uint32_t len);

/* Drain the NIC. Called from the network thread. */
void net_poll(void);

/* The polling thread's entry point. */
void net_thread(void* arg);

/* Send an ICMP echo request; the reply is counted and logged. */
int  net_ping(uint32_t dest_ip);

void net_stats(void);

uint32_t net_rx_count(void);
uint32_t net_tx_count(void);
uint32_t net_ping_replies(void);

#endif
