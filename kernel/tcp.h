/* TCP.
 *
 * Client and server sides of the state machine, with retransmission. What
 * it doesn't have, deliberately: a send window (one segment is outstanding
 * at a time), reassembly of out-of-order segments (they're dropped and the
 * peer retransmits), congestion control, and window scaling. Each of those
 * is a real subject rather than a missing line, and the parts that are here
 * are the parts that make the other end's stack agree with us.
 *
 * The point of a connection is that both ends believe the same thing about
 * what has been delivered. Everything below - sequence numbers, the ack
 * that moves snd_una, the retransmit timer - exists to keep that true when
 * the wire loses, duplicates or reorders. */

#ifndef TCP_H
#define TCP_H

#include <stdint.h>

#define TCP_MSS        1400
#define TCP_RXBUF      8192
#define TCP_MAX_CONNS  4

enum {
    TCP_CLOSED = 0,
    TCP_LISTEN,
    TCP_SYN_SENT,
    TCP_SYN_RCVD,
    TCP_ESTABLISHED,
    TCP_FIN_WAIT_1,
    TCP_FIN_WAIT_2,
    TCP_CLOSE_WAIT,
    TCP_CLOSING,
    TCP_LAST_ACK,
    TCP_TIME_WAIT
};

void tcp_initialize(void);

/* Called by the IP layer for protocol 6, and by the network thread on every
 * poll so timers can fire. */
void tcp_input(uint32_t src_ip, const uint8_t* segment, uint32_t len);
void tcp_tick(void);

/* Active open. Blocks until the handshake completes or the timeout runs
 * out; returns a connection id, or -1. */
int  tcp_connect(uint32_t ip, uint16_t port, uint32_t timeout_ms);

/* Passive open. One connection at a time - a real backlog would allocate a
 * child TCB per SYN, this reuses the listening one. */
int  tcp_listen(uint16_t port);
int  tcp_accept(int id, uint32_t timeout_ms);

int  tcp_send(int id, const void* data, uint32_t len);

/* Returns bytes copied, 0 on timeout with the connection still open, and
 * -1 once it's closed and drained. */
int  tcp_recv(int id, void* buf, uint32_t len, uint32_t timeout_ms);

void tcp_close(int id);

int         tcp_state(int id);
const char* tcp_state_name(int state);
void        tcp_stats(void);

#endif
