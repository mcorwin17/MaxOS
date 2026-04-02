#include <stdint.h>

#include "tcp.h"
#include "net.h"
#include "pit.h"
#include "serial.h"
#include "thread.h"
#include "spinlock.h"

#define IP_PROTO_TCP 6

#define FIN 0x01
#define SYN 0x02
#define RST 0x04
#define PSH 0x08
#define ACK 0x10

#define RTX_INTERVAL_MS   500
#define RTX_MAX_TRIES     6
#define TIME_WAIT_MS      2000      /* not 2*MSL; this is a LAN with an
                                     * emulator on it, not the internet */

struct tcp_header {
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t seq;
    uint32_t ack;
    uint8_t  offset;        /* header length in dwords, high nibble */
    uint8_t  flags;
    uint16_t window;
    uint16_t checksum;
    uint16_t urgent;
} __attribute__((packed));

struct tcp_conn {
    int      used;
    int      state;

    uint32_t remote_ip;
    uint16_t local_port;
    uint16_t remote_port;

    /* Send side. snd_una is the oldest byte the peer hasn't acked; snd_nxt
     * is the next one we'll put on the wire. They differ exactly when
     * something is in flight. */
    uint32_t snd_una;
    uint32_t snd_nxt;
    uint32_t rcv_nxt;
    uint16_t snd_wnd;

    /* One outstanding segment, kept so it can be sent again. A window would
     * make this a queue; stop-and-wait makes it a single buffer. */
    uint8_t  rtx[TCP_MSS];
    uint32_t rtx_len;
    uint32_t rtx_seq;
    uint8_t  rtx_flags;
    uint32_t rtx_deadline;
    int      rtx_tries;

    uint8_t  rx[TCP_RXBUF];
    uint32_t rx_head;       /* where the stack writes */
    uint32_t rx_tail;       /* where the reader reads */

    uint32_t timer_deadline;    /* TIME_WAIT, and connect/accept giving up */
    int      peer_finished;     /* FIN seen: no more data is ever coming */
};

static struct tcp_conn conns[TCP_MAX_CONNS];
static struct spinlock tcp_lock;
static uint16_t next_port = 49152;
static uint32_t segs_in, segs_out, rtx_count, bad_checksums;

static uint16_t hton16(uint16_t v) { return (uint16_t)((v << 8) | (v >> 8)); }
static uint16_t ntoh16(uint16_t v) { return hton16(v); }

static uint32_t hton32(uint32_t v) {
    return ((v & 0xFF) << 24) | ((v & 0xFF00) << 8) |
           ((v >> 8) & 0xFF00) | ((v >> 24) & 0xFF);
}
static uint32_t ntoh32(uint32_t v) { return hton32(v); }

/* Sequence numbers wrap, so "later" is a signed difference and never a
 * plain >. At 32 bits this only bites on long connections, which makes it
 * exactly the kind of bug that shows up once and can't be reproduced. */
static int seq_after(uint32_t a, uint32_t b)   { return (int32_t)(a - b) > 0; }
static int seq_after_eq(uint32_t a, uint32_t b) { return (int32_t)(a - b) >= 0; }

/* TCP's checksum covers a pseudo-header that isn't on the wire: the two
 * addresses, the protocol, and the segment length. That's what stops a
 * correctly-checksummed segment being accepted by the wrong host. */
static uint16_t tcp_checksum(uint32_t src, uint32_t dst,
                             const uint8_t* seg, uint32_t len) {
    uint32_t sum = 0;

    sum += (src >> 16) & 0xFFFF;
    sum += src & 0xFFFF;
    sum += (dst >> 16) & 0xFFFF;
    sum += dst & 0xFFFF;
    sum += IP_PROTO_TCP;
    sum += len;

    uint32_t i = 0;
    for (; i + 1 < len; i += 2) {
        sum += ((uint32_t)seg[i] << 8) | seg[i + 1];
    }
    if (i < len) sum += (uint32_t)seg[i] << 8;

    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);

    return (uint16_t)~sum;
}

static struct tcp_conn* conn_of(int id) {
    if (id < 0 || id >= TCP_MAX_CONNS) return 0;
    if (!conns[id].used) return 0;
    return &conns[id];
}

static int alloc_conn(void) {
    for (int i = 0; i < TCP_MAX_CONNS; ++i) {
        if (!conns[i].used) {
            struct tcp_conn* c = &conns[i];

            /* Zero by hand rather than trusting whatever the last
             * connection left behind. */
            c->state = TCP_CLOSED;
            c->remote_ip = 0;
            c->local_port = c->remote_port = 0;
            c->snd_una = c->snd_nxt = c->rcv_nxt = 0;
            c->snd_wnd = 0;
            c->rtx_len = c->rtx_seq = 0;
            c->rtx_flags = 0;
            c->rtx_deadline = 0;
            c->rtx_tries = 0;
            c->rx_head = c->rx_tail = 0;
            c->timer_deadline = 0;
            c->peer_finished = 0;
            c->used = 1;
            return i;
        }
    }
    return -1;
}

static uint32_t rx_used(const struct tcp_conn* c) {
    return (c->rx_head - c->rx_tail) & (TCP_RXBUF - 1);
}

static uint32_t rx_free(const struct tcp_conn* c) {
    return TCP_RXBUF - 1 - rx_used(c);
}

/* Builds and sends one segment. Sequence numbers come from the caller
 * because a retransmit has to reuse the original one, not the current
 * snd_nxt. */
static void send_segment(struct tcp_conn* c, uint8_t flags, uint32_t seq,
                         const uint8_t* data, uint32_t len) {
    uint8_t buf[TCP_MSS + 40];
    struct tcp_header* h = (struct tcp_header*)buf;

    uint32_t header_len = sizeof(*h);

    h->src_port = hton16(c->local_port);
    h->dst_port = hton16(c->remote_port);
    h->seq      = hton32(seq);
    h->ack      = (flags & ACK) ? hton32(c->rcv_nxt) : 0;
    h->flags    = flags;
    h->window   = hton16((uint16_t)rx_free(c));
    h->checksum = 0;
    h->urgent   = 0;

    /* Announce our MSS on the SYN. Without it the peer assumes 536 and
     * everything still works, just in smaller pieces. */
    if (flags & SYN) {
        buf[header_len + 0] = 2;    /* kind: maximum segment size */
        buf[header_len + 1] = 4;    /* length, including these two bytes */
        buf[header_len + 2] = (uint8_t)(TCP_MSS >> 8);
        buf[header_len + 3] = (uint8_t)(TCP_MSS & 0xFF);
        header_len += 4;
    }

    h->offset = (uint8_t)((header_len / 4) << 4);

    for (uint32_t i = 0; i < len; ++i) buf[header_len + i] = data[i];

    uint32_t total = header_len + len;
    h->checksum = hton16(tcp_checksum(net_our_ip(), c->remote_ip, buf, total));

    net_send_ip(c->remote_ip, IP_PROTO_TCP, buf, total);
    segs_out++;
}

/* Anything that consumes sequence space has to be re-sendable. A bare ACK
 * doesn't, which is why it never lands here. */
static void send_and_arm(struct tcp_conn* c, uint8_t flags,
                         const uint8_t* data, uint32_t len) {
    c->rtx_seq   = c->snd_nxt;
    c->rtx_flags = flags;
    c->rtx_len   = len;
    for (uint32_t i = 0; i < len; ++i) c->rtx[i] = data[i];

    c->rtx_deadline = pit_uptime_ms() + RTX_INTERVAL_MS;
    c->rtx_tries    = 0;

    send_segment(c, flags, c->snd_nxt, data, len);

    c->snd_nxt += len;
    if (flags & (SYN | FIN)) c->snd_nxt += 1;   /* both take one number */
}

static void send_reset(uint32_t dst_ip, uint16_t local_port,
                       uint16_t remote_port, uint32_t seq) {
    struct tcp_conn tmp;
    tmp.remote_ip   = dst_ip;
    tmp.local_port  = local_port;
    tmp.remote_port = remote_port;
    tmp.rcv_nxt     = 0;
    tmp.rx_head = tmp.rx_tail = 0;

    send_segment(&tmp, RST, seq, 0, 0);
}

void tcp_initialize(void) {
    spin_init(&tcp_lock, "tcp");
    for (int i = 0; i < TCP_MAX_CONNS; ++i) conns[i].used = 0;
    segs_in = segs_out = rtx_count = bad_checksums = 0;
}

static struct tcp_conn* find_conn(uint32_t src_ip, uint16_t src_port,
                                  uint16_t dst_port) {
    /* Exact match first: an established connection outranks a listener on
     * the same port. */
    for (int i = 0; i < TCP_MAX_CONNS; ++i) {
        struct tcp_conn* c = &conns[i];
        if (c->used && c->state != TCP_LISTEN &&
            c->remote_ip == src_ip && c->remote_port == src_port &&
            c->local_port == dst_port) {
            return c;
        }
    }
    for (int i = 0; i < TCP_MAX_CONNS; ++i) {
        struct tcp_conn* c = &conns[i];
        if (c->used && c->state == TCP_LISTEN && c->local_port == dst_port) {
            return c;
        }
    }
    return 0;
}

static void deliver(struct tcp_conn* c, const uint8_t* data, uint32_t len) {
    uint32_t room = rx_free(c);
    if (len > room) len = room;     /* the window told them not to; drop the
                                     * overflow rather than corrupt the ring */

    for (uint32_t i = 0; i < len; ++i) {
        c->rx[c->rx_head] = data[i];
        c->rx_head = (c->rx_head + 1) & (TCP_RXBUF - 1);
    }
    c->rcv_nxt += len;
}

static void ack_processing(struct tcp_conn* c, uint32_t ack) {
    if (!seq_after(ack, c->snd_una)) return;    /* old or duplicate */
    if (seq_after(ack, c->snd_nxt)) return;     /* acking what we never sent */

    c->snd_una = ack;

    /* Everything we were holding for retransmission has arrived. */
    if (seq_after_eq(ack, c->rtx_seq + c->rtx_len +
                          ((c->rtx_flags & (SYN | FIN)) ? 1 : 0))) {
        c->rtx_len   = 0;
        c->rtx_flags = 0;
        c->rtx_tries = 0;
    }
}

void tcp_input(uint32_t src_ip, const uint8_t* segment, uint32_t len) {
    if (len < sizeof(struct tcp_header)) return;

    const struct tcp_header* h = (const struct tcp_header*)segment;

    if (tcp_checksum(src_ip, net_our_ip(), segment, len) != 0) {
        bad_checksums++;
        return;
    }

    uint32_t header_len = (uint32_t)(h->offset >> 4) * 4;
    if (header_len < sizeof(*h) || header_len > len) return;

    uint16_t src_port = ntoh16(h->src_port);
    uint16_t dst_port = ntoh16(h->dst_port);
    uint32_t seq      = ntoh32(h->seq);
    uint32_t ack      = ntoh32(h->ack);
    uint8_t  flags    = h->flags;

    const uint8_t* data = segment + header_len;
    uint32_t data_len   = len - header_len;

    segs_in++;

    uint32_t irq = spin_lock_irq(&tcp_lock);

    struct tcp_conn* c = find_conn(src_ip, src_port, dst_port);

    if (!c) {
        spin_unlock_irq(&tcp_lock, irq);
        /* Answering an unknown segment with RST is what makes a closed port
         * report closed instead of hanging. Never answer a RST with a RST. */
        if (!(flags & RST)) {
            send_reset(src_ip, dst_port, src_port, (flags & ACK) ? ack : 0);
        }
        return;
    }

    if (flags & RST) {
        c->state = TCP_CLOSED;
        c->peer_finished = 1;
        spin_unlock_irq(&tcp_lock, irq);
        return;
    }

    switch (c->state) {

    case TCP_LISTEN:
        if (flags & SYN) {
            c->remote_ip   = src_ip;
            c->remote_port = src_port;
            c->rcv_nxt     = seq + 1;
            c->snd_una     = c->snd_nxt = pit_uptime_ms() * 251 + 1;
            c->snd_wnd     = ntoh16(h->window);
            c->state       = TCP_SYN_RCVD;
            send_and_arm(c, SYN | ACK, 0, 0);
        }
        break;

    case TCP_SYN_SENT:
        if ((flags & SYN) && (flags & ACK)) {
            if (ack != c->snd_nxt) break;       /* not for this handshake */
            c->rcv_nxt = seq + 1;
            c->snd_wnd = ntoh16(h->window);
            ack_processing(c, ack);
            c->state = TCP_ESTABLISHED;
            send_segment(c, ACK, c->snd_nxt, 0, 0);
        }
        break;

    case TCP_SYN_RCVD:
        if (flags & ACK) {
            ack_processing(c, ack);
            c->state = TCP_ESTABLISHED;
        }
        break;

    case TCP_ESTABLISHED:
    case TCP_FIN_WAIT_1:
    case TCP_FIN_WAIT_2:
    case TCP_CLOSE_WAIT:
    case TCP_CLOSING:
    case TCP_LAST_ACK: {
        if (flags & ACK) {
            ack_processing(c, ack);
            c->snd_wnd = ntoh16(h->window);
        }

        int need_ack = 0;

        if (data_len > 0) {
            if (seq == c->rcv_nxt) {
                deliver(c, data, data_len);
                need_ack = 1;
            } else {
                /* Out of order. No reassembly queue, so re-announce what we
                 * do have and let the peer resend from there. Wasteful, and
                 * correct - which is the right order to get them in. */
                need_ack = 1;
            }
        }

        if ((flags & FIN) && seq + data_len == c->rcv_nxt) {
            c->rcv_nxt += 1;
            c->peer_finished = 1;
            need_ack = 1;

            if (c->state == TCP_ESTABLISHED)      c->state = TCP_CLOSE_WAIT;
            else if (c->state == TCP_FIN_WAIT_1)  c->state = TCP_CLOSING;
            else if (c->state == TCP_FIN_WAIT_2) {
                c->state = TCP_TIME_WAIT;
                c->timer_deadline = pit_uptime_ms() + TIME_WAIT_MS;
            }
        }

        /* Our own FIN being acked is what moves us along, and it's only
         * acked once snd_una has passed every byte plus the FIN. */
        if (c->rtx_len == 0 && !(c->rtx_flags)) {
            if (c->state == TCP_FIN_WAIT_1 && seq_after_eq(c->snd_una, c->snd_nxt)) {
                c->state = TCP_FIN_WAIT_2;
            } else if (c->state == TCP_CLOSING &&
                       seq_after_eq(c->snd_una, c->snd_nxt)) {
                c->state = TCP_TIME_WAIT;
                c->timer_deadline = pit_uptime_ms() + TIME_WAIT_MS;
            } else if (c->state == TCP_LAST_ACK &&
                       seq_after_eq(c->snd_una, c->snd_nxt)) {
                c->state = TCP_CLOSED;
            }
        }

        if (need_ack) send_segment(c, ACK, c->snd_nxt, 0, 0);
        break;
    }

    case TCP_TIME_WAIT:
        /* A retransmitted FIN means our last ACK was lost. Send it again
         * and restart the wait. */
        if (flags & FIN) {
            send_segment(c, ACK, c->snd_nxt, 0, 0);
            c->timer_deadline = pit_uptime_ms() + TIME_WAIT_MS;
        }
        break;

    default:
        break;
    }

    spin_unlock_irq(&tcp_lock, irq);
}

void tcp_tick(void) {
    uint32_t now = pit_uptime_ms();
    uint32_t irq = spin_lock_irq(&tcp_lock);

    for (int i = 0; i < TCP_MAX_CONNS; ++i) {
        struct tcp_conn* c = &conns[i];
        if (!c->used) continue;

        if (c->state == TCP_TIME_WAIT && now >= c->timer_deadline) {
            c->state = TCP_CLOSED;
            continue;
        }

        if (c->rtx_flags == 0 && c->rtx_len == 0) continue;
        if (now < c->rtx_deadline) continue;

        if (++c->rtx_tries > RTX_MAX_TRIES) {
            /* Nobody is listening. Better to fail the connection than to
             * keep a dead one in the table forever. */
            c->state = TCP_CLOSED;
            c->peer_finished = 1;
            c->rtx_flags = 0;
            c->rtx_len = 0;
            continue;
        }

        send_segment(c, c->rtx_flags, c->rtx_seq, c->rtx, c->rtx_len);
        rtx_count++;
        c->rtx_deadline = now + RTX_INTERVAL_MS;
    }

    spin_unlock_irq(&tcp_lock, irq);
}

int tcp_connect(uint32_t ip, uint16_t port, uint32_t timeout_ms) {
    uint32_t irq = spin_lock_irq(&tcp_lock);

    int id = alloc_conn();
    if (id < 0) { spin_unlock_irq(&tcp_lock, irq); return -1; }

    struct tcp_conn* c = &conns[id];
    c->remote_ip   = ip;
    c->remote_port = port;
    c->local_port  = next_port++;
    if (next_port == 0) next_port = 49152;

    /* A fixed ISN would let a stale segment from a previous connection on
     * the same ports land in this one. */
    c->snd_una = c->snd_nxt = pit_uptime_ms() * 1103515245u + 12345u;
    c->state   = TCP_SYN_SENT;

    send_and_arm(c, SYN, 0, 0);
    spin_unlock_irq(&tcp_lock, irq);

    uint32_t deadline = pit_uptime_ms() + timeout_ms;
    while (pit_uptime_ms() < deadline) {
        if (c->state == TCP_ESTABLISHED) return id;
        if (c->state == TCP_CLOSED) break;
        thread_sleep_ms(10);
    }

    tcp_close(id);
    return -1;
}

int tcp_listen(uint16_t port) {
    uint32_t irq = spin_lock_irq(&tcp_lock);

    int id = alloc_conn();
    if (id < 0) { spin_unlock_irq(&tcp_lock, irq); return -1; }

    conns[id].local_port = port;
    conns[id].state      = TCP_LISTEN;

    spin_unlock_irq(&tcp_lock, irq);
    return id;
}

int tcp_accept(int id, uint32_t timeout_ms) {
    struct tcp_conn* c = conn_of(id);
    if (!c) return -1;

    uint32_t deadline = pit_uptime_ms() + timeout_ms;
    while (pit_uptime_ms() < deadline) {
        if (c->state == TCP_ESTABLISHED) return id;
        if (c->state == TCP_CLOSED) return -1;
        thread_sleep_ms(10);
    }
    return -1;
}

int tcp_send(int id, const void* data, uint32_t len) {
    struct tcp_conn* c = conn_of(id);
    if (!c) return -1;

    const uint8_t* p = (const uint8_t*)data;
    uint32_t sent = 0;

    while (sent < len) {
        if (c->state != TCP_ESTABLISHED && c->state != TCP_CLOSE_WAIT) {
            return sent ? (int)sent : -1;
        }

        /* Stop and wait: one segment in flight, so the next one can't go
         * until this one is acked. A window would keep several moving. */
        if (c->rtx_len || c->rtx_flags) { thread_sleep_ms(5); continue; }

        uint32_t chunk = len - sent;
        if (chunk > TCP_MSS) chunk = TCP_MSS;

        uint32_t irq = spin_lock_irq(&tcp_lock);
        send_and_arm(c, ACK | PSH, p + sent, chunk);
        spin_unlock_irq(&tcp_lock, irq);

        sent += chunk;
    }

    /* Don't return until the peer has acknowledged the lot - otherwise a
     * close() right behind this would send FIN with data still unacked. */
    uint32_t deadline = pit_uptime_ms() + 5000;
    while ((c->rtx_len || c->rtx_flags) && pit_uptime_ms() < deadline) {
        thread_sleep_ms(5);
    }

    return (int)sent;
}

int tcp_recv(int id, void* buf, uint32_t len, uint32_t timeout_ms) {
    struct tcp_conn* c = conn_of(id);
    if (!c) return -1;

    uint32_t deadline = pit_uptime_ms() + timeout_ms;
    uint8_t* out = (uint8_t*)buf;

    for (;;) {
        uint32_t irq = spin_lock_irq(&tcp_lock);
        uint32_t have = rx_used(c);

        if (have) {
            uint32_t n = have < len ? have : len;
            for (uint32_t i = 0; i < n; ++i) {
                out[i] = c->rx[c->rx_tail];
                c->rx_tail = (c->rx_tail + 1) & (TCP_RXBUF - 1);
            }
            spin_unlock_irq(&tcp_lock, irq);
            return (int)n;
        }
        spin_unlock_irq(&tcp_lock, irq);

        /* Drained and the peer said it's finished: that's EOF, not a
         * timeout. The order matters - data queued before the FIN has to
         * come out first. */
        if (c->peer_finished || c->state == TCP_CLOSED) return -1;
        if (pit_uptime_ms() >= deadline) return 0;

        thread_sleep_ms(5);
    }
}

void tcp_close(int id) {
    struct tcp_conn* c = conn_of(id);
    if (!c) return;

    uint32_t irq = spin_lock_irq(&tcp_lock);

    if (c->state == TCP_ESTABLISHED || c->state == TCP_SYN_RCVD) {
        c->state = TCP_FIN_WAIT_1;
        send_and_arm(c, FIN | ACK, 0, 0);
        spin_unlock_irq(&tcp_lock, irq);
    } else if (c->state == TCP_CLOSE_WAIT) {
        c->state = TCP_LAST_ACK;
        send_and_arm(c, FIN | ACK, 0, 0);
        spin_unlock_irq(&tcp_lock, irq);
    } else {
        c->state = TCP_CLOSED;
        c->used  = 0;
        spin_unlock_irq(&tcp_lock, irq);
        return;
    }

    /* Give the shutdown a chance to finish before the slot is reused; a
     * connection torn down cleanly is one the peer doesn't have to time
     * out. */
    uint32_t deadline = pit_uptime_ms() + 3000;
    while (pit_uptime_ms() < deadline) {
        if (c->state == TCP_CLOSED || c->state == TCP_TIME_WAIT) break;
        thread_sleep_ms(10);
    }

    irq = spin_lock_irq(&tcp_lock);
    c->used = 0;
    c->state = TCP_CLOSED;
    spin_unlock_irq(&tcp_lock, irq);
}

int tcp_state(int id) {
    struct tcp_conn* c = conn_of(id);
    return c ? c->state : TCP_CLOSED;
}

const char* tcp_state_name(int state) {
    switch (state) {
    case TCP_CLOSED:      return "CLOSED";
    case TCP_LISTEN:      return "LISTEN";
    case TCP_SYN_SENT:    return "SYN_SENT";
    case TCP_SYN_RCVD:    return "SYN_RCVD";
    case TCP_ESTABLISHED: return "ESTABLISHED";
    case TCP_FIN_WAIT_1:  return "FIN_WAIT_1";
    case TCP_FIN_WAIT_2:  return "FIN_WAIT_2";
    case TCP_CLOSE_WAIT:  return "CLOSE_WAIT";
    case TCP_CLOSING:     return "CLOSING";
    case TCP_LAST_ACK:    return "LAST_ACK";
    case TCP_TIME_WAIT:   return "TIME_WAIT";
    default:              return "?";
    }
}

void tcp_stats(void) {
    kprintf("  segments  in %u, out %u, retransmitted %u\n",
            segs_in, segs_out, rtx_count);
    kprintf("  bad checksums %u\n", bad_checksums);

    int any = 0;
    for (int i = 0; i < TCP_MAX_CONNS; ++i) {
        struct tcp_conn* c = &conns[i];
        if (!c->used) continue;
        any = 1;
        kprintf("  [%d] %s port %u -> %u.%u.%u.%u:%u, %u buffered\n",
                i, tcp_state_name(c->state), c->local_port,
                (c->remote_ip >> 24) & 0xFF, (c->remote_ip >> 16) & 0xFF,
                (c->remote_ip >> 8) & 0xFF, c->remote_ip & 0xFF,
                c->remote_port, rx_used(c));
    }
    if (!any) kprintf("  no connections\n");
}
