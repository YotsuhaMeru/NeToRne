#include "raw_framing.h"
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    unsigned int packet[48], before[48], media_words[16];
    unsigned char state[7];
    unsigned char *media = (unsigned char *)media_words;
    const unsigned int modes[] = {0, 1, 2, 3, 255};
    unsigned int pid, af, sc, mode, i, length, marker, expected;
    for (mode = 0; mode < sizeof modes / sizeof modes[0]; ++mode)
    for (pid = 0; pid < 8192; ++pid)
    for (af = 0; af < 4; ++af)
    for (sc = 0; sc < 4; ++sc) {
        for (i = 0; i < 48; ++i) packet[i] = 0xa5a5a5a5u + i * 0x01020304u;
        length = (pid * 17u + af) & 255u;
        packet[1] = 0x47e00000u | (pid << 8) | (sc << 6) | (af << 4) | (pid & 15u);
        /* raw_packet reads byte8 in host order. Set that byte explicitly;
         * framing's header word uses the target's numeric big-endian value. */
        ((unsigned char *)packet)[8] = (unsigned char)length;
        memcpy(before, packet, sizeof packet);
        memset(media_words, 0xa5, sizeof media_words);
        media[0] = (unsigned char)modes[mode];
        media_words[6] = 0x00123400u;
        memset(state, 0xa5, sizeof state);
        marker = af != 2 && pid != 8191 &&
                 (modes[mode] == 2 || (modes[mode] == 1 && sc)) ? 0x40u : 0u;
        expected = (before[1] & ~0xc0u) | marker;
        /* The host is little-endian, so it cannot also model raw_packet's
         * bytewise header read with this numeric word. Assert framing only;
         * the separate exact FR30 test checks classification on BE memory. */
        (void)tr_raw_framing(packet, media, state);
        assert(packet[1] == expected);
        before[1] = expected;
        assert(memcmp(before, packet, sizeof packet) == 0);
        assert(media_words[5] == (marker ? 0x00123400u : 0u));
        assert(media_words[12] == marker);
        assert(media_words[6] == 0x00123400u);
        for (i = 1; i < 16; ++i)
            if (i != 5 && i != 6 && i != 12) assert(media_words[i] == 0xa5a5a5a5u);
        assert(media[0] == modes[mode]);
    }
    puts("USB framing: 655,360 mode/PID/adaptation/scrambling combinations passed");
    return 0;
}
