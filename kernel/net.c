#include <stdint.h>

#include "net.h"
#include "tcp.h"
#include "ne2000.h"
#include "serial.h"
#include "console.h"
#include "thread.h"

#define ETH_P_IP   0x0800
#define ETH_P_ARP  0x0806

#define ARP_REQUEST 1
#define ARP_REPLY   2

#define IP_PROTO_ICMP 1
#define IP_PROTO_TCP  6

#define ICMP_ECHO_REPLY   0
#define ICMP_ECHO_REQUEST 8

#define ARP_CACHE_SIZE 8

struct eth_header {
    uint8_t  dst[ETH_ALEN];
    uint8_t  src[ETH_ALEN];
    uint16_t type;
} __attribute__((packed));

struct arp_packet {
    uint16_t htype, ptype;
    uint8_t  hlen, plen;
    uint16_t oper;
    uint8_t  sender_mac[ETH_ALEN];
    uint32_t sender_ip;
    uint8_t  target_mac[ETH_ALEN];
    uint32_t target_ip;
} __attribute__((packed));

struct ip_header {
    uint8_t  version_ihl;
    uint8_t  tos;
    uint16_t total_length;
    uint16_t id;
    uint16_t flags_fragment;
    uint8_t  ttl;
    uint8_t  protocol;
    uint16_t checksum;
    uint32_t src;
    uint32_t dst;
} __attribute__((packed));

struct icmp_header {
    uint8_t  type, code;
    uint16_t checksum;
    uint16_t id, sequence;
} __attribute__((packed));

struct arp_entry {
    uint32_t ip;
    uint8_t  mac[ETH_ALEN];
    int      valid;
};

static struct arp_entry arp_cache[ARP_CACHE_SIZE];
static uint32_t rx_count, tx_count, ping_replies, arp_replies;
static uint16_t ping_sequence;
static uint32_t our_ip = NET_OUR_IP;

static const uint8_t broadcast_mac[ETH_ALEN] =
    { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

/* The wire is big-endian; x86 isn't. Every field crosses one of these. */
static uint16_t hton16(uint16_t v) { return (uint16_t)((v << 8) | (v >> 8)); }
static uint16_t ntoh16(uint16_t v) { return hton16(v); }

static uint32_t hton32(uint32_t v) {
    return ((v & 0xFF) << 24) | ((v & 0xFF00) << 8) |
           ((v >> 8) & 0xFF00) | ((v >> 24) & 0xFF);
}
static uint32_t ntoh32(uint32_t v) { return hton32(v); }

static void copy_mac(uint8_t* dst, const uint8_t* src) {
    for (int i = 0; i < ETH_ALEN; ++i) dst[i] = src[i];
}

static int mac_equal(const uint8_t* a, const uint8_t* b) {
    for (int i = 0; i < ETH_ALEN; ++i) if (a[i] != b[i]) return 0;
    return 1;
}

/* One's complement sum, folded. Shared by IP and ICMP - the algorithm is
 * the same, only the span differs. */
static uint16_t checksum(const void* data, uint32_t len) {
    const uint8_t* p = (const uint8_t*)data;
    uint32_t sum = 0;

    while (len > 1) {
        sum += ((uint32_t)p[0] << 8) | p[1];
        p += 2;
        len -= 2;
    }
    if (len) sum += (uint32_t)p[0] << 8;

    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);

    return (uint16_t)~sum;
}

void net_initialize(void) {
    for (int i = 0; i < ARP_CACHE_SIZE; ++i) arp_cache[i].valid = 0;
    rx_count = tx_count = ping_replies = arp_replies = 0;
    ping_sequence = 0;

    if (ne2000_present()) {
        kprintf("net: 10.0.2.15 up on ne2000\n");
    }
}

int net_up(void) { return ne2000_present(); }

uint32_t net_our_ip(void) { return our_ip; }

void net_set_ip(uint32_t ip) {
    our_ip = ip;

    /* Mappings were learned on behalf of an identity we no longer have. */
    for (int i = 0; i < ARP_CACHE_SIZE; ++i) arp_cache[i].valid = 0;

    kprintf("net: address is now %u.%u.%u.%u\n",
            (ip >> 24) & 0xFF, (ip >> 16) & 0xFF, (ip >> 8) & 0xFF, ip & 0xFF);
}

static void arp_learn(uint32_t ip, const uint8_t* mac) {
    for (int i = 0; i < ARP_CACHE_SIZE; ++i) {
        if (arp_cache[i].valid && arp_cache[i].ip == ip) {
            copy_mac(arp_cache[i].mac, mac);
            return;
        }
    }
    for (int i = 0; i < ARP_CACHE_SIZE; ++i) {
        if (!arp_cache[i].valid) {
            arp_cache[i].ip = ip;
            copy_mac(arp_cache[i].mac, mac);
            arp_cache[i].valid = 1;
            return;
        }
    }
    /* Full: overwrite the first. A real cache would age entries. */
    arp_cache[0].ip = ip;
    copy_mac(arp_cache[0].mac, mac);
}

static const uint8_t* arp_lookup(uint32_t ip) {
    for (int i = 0; i < ARP_CACHE_SIZE; ++i) {
        if (arp_cache[i].valid && arp_cache[i].ip == ip) return arp_cache[i].mac;
    }
    return 0;
}

static int send_frame(const uint8_t* dst_mac, uint16_t type,
                      const void* payload, uint32_t len) {
    uint8_t frame[ETH_FRAME_MAX];
    struct eth_header* eth = (struct eth_header*)frame;

    copy_mac(eth->dst, dst_mac);
    copy_mac(eth->src, ne2000_mac());
    eth->type = hton16(type);

    const uint8_t* p = (const uint8_t*)payload;
    for (uint32_t i = 0; i < len; ++i) frame[sizeof(*eth) + i] = p[i];

    if (ne2000_send(frame, sizeof(*eth) + len) != 0) return -1;

    tx_count++;
    return 0;
}

static void send_arp(uint16_t oper, const uint8_t* target_mac,
                     uint32_t target_ip) {
    struct arp_packet arp;

    arp.htype = hton16(1);              /* ethernet */
    arp.ptype = hton16(ETH_P_IP);
    arp.hlen  = ETH_ALEN;
    arp.plen  = 4;
    arp.oper  = hton16(oper);
    copy_mac(arp.sender_mac, ne2000_mac());
    arp.sender_ip = hton32(our_ip);
    copy_mac(arp.target_mac, target_mac);
    arp.target_ip = hton32(target_ip);

    send_frame(oper == ARP_REQUEST ? broadcast_mac : target_mac,
               ETH_P_ARP, &arp, sizeof(arp));
}

static void handle_arp(const uint8_t* payload, uint32_t len) {
    if (len < sizeof(struct arp_packet)) return;

    const struct arp_packet* arp = (const struct arp_packet*)payload;

    if (ntoh16(arp->ptype) != ETH_P_IP) return;

    uint32_t sender_ip = ntoh32(arp->sender_ip);
    uint32_t target_ip = ntoh32(arp->target_ip);

    /* Learn from anything addressed to us, request or reply - the sender's
     * mapping is good either way. */
    if (target_ip == our_ip) arp_learn(sender_ip, arp->sender_mac);

    if (ntoh16(arp->oper) == ARP_REQUEST && target_ip == our_ip) {
        send_arp(ARP_REPLY, arp->sender_mac, sender_ip);
    } else if (ntoh16(arp->oper) == ARP_REPLY) {
        arp_replies++;
    }
}

void net_send_ip(uint32_t dst_ip, uint8_t protocol,
                 const void* payload, uint32_t len) {
    const uint8_t* dst_mac = arp_lookup(dst_ip);
    if (!dst_mac) {
        /* No mapping yet: ask, and drop this one. A real stack would queue
         * the packet; at ping cadence the retry is the next ping. */
        send_arp(ARP_REQUEST, broadcast_mac, dst_ip);
        return;
    }

    uint8_t buf[ETH_MTU];
    struct ip_header* ip = (struct ip_header*)buf;

    ip->version_ihl    = 0x45;          /* IPv4, 5 dwords of header */
    ip->tos            = 0;
    ip->total_length   = hton16((uint16_t)(sizeof(*ip) + len));
    ip->id             = hton16(0);
    ip->flags_fragment = hton16(0x4000);    /* don't fragment */
    ip->ttl            = 64;
    ip->protocol       = protocol;
    ip->checksum       = 0;
    ip->src            = hton32(our_ip);
    ip->dst            = hton32(dst_ip);

    /* Computed over the header only, and only once the field itself is
     * zero - it's part of its own input. */
    ip->checksum = hton16(checksum(ip, sizeof(*ip)));

    const uint8_t* p = (const uint8_t*)payload;
    for (uint32_t i = 0; i < len; ++i) buf[sizeof(*ip) + i] = p[i];

    send_frame(dst_mac, ETH_P_IP, buf, sizeof(*ip) + len);
}

static void handle_icmp(uint32_t src_ip, const uint8_t* payload,
                        uint32_t len) {
    if (len < sizeof(struct icmp_header)) return;

    const struct icmp_header* icmp = (const struct icmp_header*)payload;

    if (icmp->type == ICMP_ECHO_REQUEST) {
        /* Reply with the same body: the sender checks it comes back
         * unchanged, and ping prints the round trip on that basis. */
        uint8_t reply[ETH_MTU];
        struct icmp_header* out = (struct icmp_header*)reply;

        for (uint32_t i = 0; i < len; ++i) reply[i] = payload[i];

        out->type     = ICMP_ECHO_REPLY;
        out->code     = 0;
        out->checksum = 0;
        out->checksum = hton16(checksum(reply, len));

        net_send_ip(src_ip, IP_PROTO_ICMP, reply, len);

        kprintf("net: echo request from %u.%u.%u.%u, replied\n",
                (src_ip >> 24) & 0xFF, (src_ip >> 16) & 0xFF,
                (src_ip >> 8) & 0xFF, src_ip & 0xFF);
    } else if (icmp->type == ICMP_ECHO_REPLY) {
        ping_replies++;
        kprintf("net: echo reply from %u.%u.%u.%u seq %u\n",
                (src_ip >> 24) & 0xFF, (src_ip >> 16) & 0xFF,
                (src_ip >> 8) & 0xFF, src_ip & 0xFF,
                ntoh16(icmp->sequence));
    }
}

static void handle_ip(const uint8_t* payload, uint32_t len) {
    if (len < sizeof(struct ip_header)) return;

    const struct ip_header* ip = (const struct ip_header*)payload;

    if ((ip->version_ihl >> 4) != 4) return;

    uint32_t header_len = (uint32_t)(ip->version_ihl & 0x0F) * 4;
    if (header_len < sizeof(*ip) || header_len > len) return;

    /* A correct header sums to zero with its own checksum included - which
     * is the neat part of the one's complement scheme. */
    if (checksum(ip, header_len) != 0) return;

    uint32_t total = ntoh16(ip->total_length);
    if (total > len || total < header_len) return;

    uint32_t dst = ntoh32(ip->dst);
    if (dst != our_ip) return;

    if (ip->protocol == IP_PROTO_ICMP) {
        handle_icmp(ntoh32(ip->src), payload + header_len,
                    total - header_len);
    } else if (ip->protocol == IP_PROTO_TCP) {
        tcp_input(ntoh32(ip->src), payload + header_len,
                  total - header_len);
    }
}

void net_input(const uint8_t* frame, uint32_t len) {
    if (len < sizeof(struct eth_header)) return;

    const struct eth_header* eth = (const struct eth_header*)frame;

    /* Ours or broadcast only. The card filters too, but promiscuous modes
     * and multicast leak through. */
    if (!mac_equal(eth->dst, ne2000_mac()) &&
        !mac_equal(eth->dst, broadcast_mac)) {
        return;
    }

    rx_count++;

    const uint8_t* payload = frame + sizeof(*eth);
    uint32_t payload_len = len - sizeof(*eth);

    switch (ntoh16(eth->type)) {
    case ETH_P_ARP: handle_arp(payload, payload_len); break;
    case ETH_P_IP:  handle_ip(payload, payload_len);  break;
    default: break;
    }
}

void net_poll(void) {
    if (!ne2000_present()) return;

    uint8_t frame[ETH_FRAME_MAX];
    uint32_t len;

    /* Bounded: a busy link shouldn't starve everything else on this CPU. */
    for (int i = 0; i < 16; ++i) {
        len = ne2000_receive(frame, sizeof(frame));
        if (!len) break;
        net_input(frame, len);
    }
}

void net_thread(void* arg) {
    (void)arg;

    /* Announce ourselves so the gateway learns our mapping without waiting
     * to be asked - and so our own cache fills from the reply. */
    send_arp(ARP_REQUEST, broadcast_mac, NET_GATEWAY_IP);

    for (;;) {
        net_poll();
        tcp_tick();             /* retransmit timers live on this thread */
        thread_sleep_ms(10);    /* 100Hz is plenty for ping cadence */
    }
}

int net_ping(uint32_t dest_ip) {
    if (!ne2000_present()) return -1;

    struct {
        struct icmp_header hdr;
        uint8_t body[32];
    } __attribute__((packed)) echo;

    echo.hdr.type     = ICMP_ECHO_REQUEST;
    echo.hdr.code     = 0;
    echo.hdr.checksum = 0;
    echo.hdr.id       = hton16(0x4D58);         /* 'MX' */
    echo.hdr.sequence = hton16(++ping_sequence);

    for (int i = 0; i < 32; ++i) echo.body[i] = (uint8_t)('a' + (i % 26));

    echo.hdr.checksum = hton16(checksum(&echo, sizeof(echo)));

    net_send_ip(dest_ip, IP_PROTO_ICMP, &echo, sizeof(echo));
    return 0;
}

uint32_t net_rx_count(void)     { return rx_count; }
uint32_t net_tx_count(void)     { return tx_count; }
uint32_t net_ping_replies(void) { return ping_replies; }

void net_stats(void) {
    const uint8_t* m = ne2000_mac();

    console_write("  link    ");
    console_write(ne2000_present() ? "up" : "down");
    console_write("\n  mac     ");
    for (int i = 0; i < ETH_ALEN; ++i) {
        static const char hex[] = "0123456789abcdef";
        if (i) console_putchar(':');
        console_putchar(hex[m[i] >> 4]);
        console_putchar(hex[m[i] & 0xF]);
    }
    console_write("\n  ip      ");
    write_decimal_console((our_ip >> 24) & 0xFF); console_putchar('.');
    write_decimal_console((our_ip >> 16) & 0xFF); console_putchar('.');
    write_decimal_console((our_ip >> 8) & 0xFF);  console_putchar('.');
    write_decimal_console(our_ip & 0xFF);
    console_putchar('\n');

    console_write("  rx      ");
    write_decimal_console(rx_count);
    console_write(" frames\n  tx      ");
    write_decimal_console(tx_count);
    console_write(" frames\n  replies ");
    write_decimal_console(ping_replies);
    console_write(" echo, ");
    write_decimal_console(arp_replies);
    console_write(" arp\n");
}
