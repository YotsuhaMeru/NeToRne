#include "raw_packet.h"
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    unsigned char packet[192], before[192], state[7], framed[192];
    unsigned int pid, af, length, i, prefix, tail;
    for (pid = 0; pid < 8192; ++pid) {
        for (af = 0; af < 4; ++af) {
            for (length = 0; length < 256; ++length) {
                for (i = 0; i < sizeof packet; ++i)
                    packet[i] = (unsigned char)(i * 73u + pid * 17u + length);
                packet[4] = 0x47;
                packet[5] = (unsigned char)((pid >> 8) | 0xe0);
                packet[6] = (unsigned char)pid;
                packet[7] = (unsigned char)(0xc0 | (af << 4) | (length & 15));
                packet[8] = (unsigned char)length;
                memcpy(before, packet, sizeof packet);
                memset(state, 0xa5, sizeof state);
                assert(tr_raw_packet_prepare(packet, state) == 0);
                assert(memcmp(before, packet, sizeof packet) == 0);
                assert(state[0] == 0 && state[1] == 0xa5 && state[2] == 0xa5);
                assert(state[5] == 0xa5 && state[6] == 0xa5);
                prefix = state[3]; tail = state[4];
                assert(prefix >= 8 && prefix <= 192 && prefix + tail == 192);
                if (af == 2) assert(prefix == 192);
                else if (af == 3) assert(prefix == (length < 183 ? length : 183) + 9);
                else assert(prefix == 8);
                /* Model concatenation of clear header / cipher payload after
                 * decryption. Neither PCR, CC, PSI nor padding may disappear. */
                memcpy(framed, packet, prefix);
                memcpy(framed + prefix, packet + prefix, tail);
                assert(memcmp(before, framed, sizeof packet) == 0);
            }
        }
    }
    memset(packet, 0xff, sizeof packet);
    packet[5] = 3;
    memcpy(before, packet, sizeof packet);
    memset(state, 0xa5, sizeof state);
    assert(tr_raw_packet_prepare(packet, state) == 1);
    assert(state[0] == 4 && state[2] == 1);
    assert(state[1] == 0xa5 && state[3] == 0xa5 && state[4] == 0xa5);
    assert(memcmp(packet, before, sizeof packet) == 0);
    puts("Raw packet: 8,388,608 PID/adaptation cases preserve all 192 bytes");
    return 0;
}
