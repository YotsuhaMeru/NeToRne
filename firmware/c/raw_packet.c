#include "raw_packet.h"

/* Preserve the existing USB media cipher's clear adaptation field and
 * encrypted payload split. This is transport framing, not broadcast CA.
 * Packet validation is owned by the legacy input engine. Even malformed
 * adaptation fields must not be repaired in a raw forwarding path. */
unsigned int tr_raw_packet_prepare(const volatile unsigned char *packet,
                                   volatile unsigned char *state)
{
    unsigned int adaptation = (packet[7] >> 4) & 3u;
    unsigned int prefix;
    /* The input engine's stop marker is not a broadcast TS packet. Retain
     * its existing class/continuation handshake with the output scheduler. */
    if (packet[4] == 0xffu && packet[5] == 3u && packet[6] == 0xffu) {
        state[0] = 4;
        state[2] = 1;
        return 1;
    }
    if (adaptation == 2u) {
        prefix = 192u;
    } else if (adaptation == 3u) {
        unsigned int length = packet[8];
        if (length > 183u) length = 183u;
        prefix = length + 9u;
    } else {
        prefix = 8u;
    }
    state[0] = 0; /* existing output DMA's unchanged-packet class */
    state[3] = (unsigned char)prefix;
    state[4] = (unsigned char)(192u - prefix);
    return 0;
}
