#ifndef TORNE_RAW_PACKET_H
#define TORNE_RAW_PACKET_H
/* The packet is the existing 4-byte timestamp + 188-byte TS buffer.
 * State points at mode8/9 a28af: class, reserved, continuation,
 * clear-prefix length, encrypted-tail length. No packet bytes are written. */
unsigned int tr_raw_packet_prepare(const volatile unsigned char *packet,
                                   volatile unsigned char *state);
#endif
