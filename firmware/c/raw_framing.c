#include "raw_framing.h"
#include "raw_packet.h"

typedef char framing_word_is_four[(sizeof(unsigned int) == 4) ? 1 : -1];

unsigned int tr_raw_framing(volatile unsigned int *packet,
                           volatile unsigned char *media,
                           volatile unsigned char *state)
{
    unsigned int header = packet[1];
    unsigned int adaptation = (header >> 4) & 3u;
    unsigned int pid = (header >> 8) & 0x1fffu;
    unsigned int enabled = media[0];
    unsigned int cipher = 0;
    unsigned int marker = 0;
    /* Exact legacy e512a..e51b4 behavior: a26a0 is a byte, a26b4,
     * a26b8 and a26d0 are words. Only the fourth header byte changes.
     * mode1 uses the incoming scrambling bits; mode2 encrypts every
     * non-NULL packet with a payload. No payload/PCR/PSI bytes change. */
    if (adaptation != 2u && pid != 0x1fffu &&
        (enabled == 2u || (enabled == 1u && (header & 0xc0u)))) {
        cipher = *(volatile unsigned int *)(media + 0x18);
        marker = 0x40u;
    }
    *(volatile unsigned int *)(media + 0x14) = cipher;
    *(volatile unsigned int *)(media + 0x30) = marker;
    packet[1] = (header & 0xffffff3fu) | marker;
    return tr_raw_packet_prepare((volatile unsigned char *)packet, state);
}
