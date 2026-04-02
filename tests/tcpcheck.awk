# Reads a pcap as decimal bytes (od -An -v -tu1) and checks the TCP
# conversation in it from the outside: every checksum recomputed here, the
# handshake read off the wire in order, and both FINs accounted for.
#
# The capture is qemu's record of what actually crossed the link, so none of
# this depends on the kernel's opinion of what it sent.

{ for (j = 1; j <= NF; j++) b[n++] = $j }

function u16(o) { return b[o] * 256 + b[o+1] }
function le32(o) { return b[o] + b[o+1]*256 + b[o+2]*65536 + b[o+3]*16777216 }

function flagstr(f,   s) {
    s = ""
    if (f % 4 >= 2)          s = s "SYN|"
    if (int(f/16) % 2 == 1)  s = s "ACK|"
    if (int(f/8) % 2 == 1)   s = s "PSH|"
    if (f % 2 == 1)          s = s "FIN|"
    if (int(f/4) % 2 == 1)   s = s "RST|"
    if (s == "") return "-"
    return substr(s, 1, length(s) - 1)
}

# One's complement sum over the pseudo-header plus the segment. A correct
# segment sums to zero with its own checksum field included, which is why
# nothing here has to know where that field is.
function tcpsum(ip, seg, len,   sum, i) {
    sum = u16(ip+12) + u16(ip+14) + u16(ip+16) + u16(ip+18) + 6 + len
    for (i = 0; i + 1 < len; i += 2) sum += u16(seg+i)
    if (i < len) sum += b[seg+i] * 256
    while (int(sum / 65536)) sum = (sum % 65536) + int(sum / 65536)
    return 65535 - sum
}

END {
    off = 24
    segs = 0; ok = 0; bad = 0; fins = 0; payload = 0

    while (off + 16 <= n) {
        caplen = le32(off + 8)
        frame  = off + 16
        if (frame + caplen > n) break
        off = frame + caplen
        if (caplen < 34) continue
        if (u16(frame + 12) != 2048) continue          # IPv4 only

        ip = frame + 14
        ihl = (b[ip] % 16) * 4
        if (b[ip + 9] != 6) continue                   # TCP only

        seg = ip + ihl
        slen = u16(ip + 2) - ihl
        if (seg + slen > n) continue

        segs++
        doff  = int(b[seg + 12] / 16) * 4
        flags = b[seg + 13]
        payload += slen - doff

        if (tcpsum(ip, seg, slen) == 0) ok++; else bad++
        if (segs <= 3) order[segs] = flagstr(flags)
        if (flags % 2 == 1) fins++
    }

    printf "  segments %d, checksums recomputed here: %d ok / %d bad\n", segs, ok, bad
    printf "  handshake %s -> %s -> %s, payload %d bytes, %d FIN\n",
           order[1], order[2], order[3], payload, fins

    bad2 = 0
    if (segs < 8)                    { print "  too few segments"; bad2 = 1 }
    if (bad > 0)                     { print "  bad checksums on the wire"; bad2 = 1 }
    if (order[1] != "SYN")           { print "  first segment is not a bare SYN"; bad2 = 1 }
    if (order[2] != "SYN|ACK")       { print "  second segment is not SYN|ACK"; bad2 = 1 }
    if (order[3] !~ /ACK/)           { print "  third segment does not ack"; bad2 = 1 }
    if (fins < 2)                    { print "  connection never closed from both ends"; bad2 = 1 }
    if (payload < 80)                { print "  no real data crossed"; bad2 = 1 }
    exit bad2
}
