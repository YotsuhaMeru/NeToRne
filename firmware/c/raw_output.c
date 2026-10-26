#include "raw_output.h"

typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;
typedef char word_size_must_be_four[(sizeof(u32) == 4) ? 1 : -1];
typedef char halfword_size_must_be_two[(sizeof(u16) == 2) ? 1 : -1];
#ifdef TR_OUTPUT_TEST
#define R8(a) tr_output_read8(a)
#define R16(a) tr_output_read16(a)
#define R32(a) tr_output_read32(a)
#define W8(a,v) tr_output_write8(a,(u8)(v))
#define W16(a,v) tr_output_write16(a,(u16)(v))
#define W32(a,v) tr_output_write32(a,(u32)(v))
#else
#define R8(a) (*(volatile u8 *)(a))
#define R16(a) (*(volatile u16 *)(a))
#define R32(a) (*(volatile u32 *)(a))
#define W8(a,v) (*(volatile u8 *)(a) = (u8)(v))
#define W16(a,v) (*(volatile u16 *)(a) = (u16)(v))
#define W32(a,v) (*(volatile u32 *)(a) = (u32)(v))
#endif

#ifndef TR_OUTPUT_PACKET_PREFIX
#define TR_OUTPUT_PACKET_PREFIX(packet, timestamp) (timestamp)
#endif

/* Keep the existing event masks / ring / USB media DMA handshake, but select
 * only the next generic TS packet. No ES packetization, PSI regeneration,
 * PCR rewrite, timestamp sorting between streams, or synthetic NULL source.
 * The companion C packet preparer supplies only classes 0 (TS) / 4 (stop).
 * No new card, descrambler, cipher key, IRQ or shared-memory allocation. */
unsigned int tr_raw_output(unsigned int events)
{
    u32 result = 0;
    u32 ready, used, producer, consumer, time, delta, packet;
    u8 output;

    if (events & 4u) {
        if (R8(0xa294c) == 2u) {
            W8(0xa294c, 3);
            W16(0x5020, (R32(0x5000) & 0x40000000u) ? 0x101u : 1u);
        }
        if (R8(0xa25f6) == 2u) {
            W8(0xa25f6, 1);
            events |= 0x20000000u;
        }
    }

    /* Feed the existing generic input ring, respecting its reserve. */
    if ((events & 0x20000000u) && R8(0xa25f6) == 1u) {
        /* Original e7004..e700e uses lduh, not word loads. Reading the
         * adjacent halfword changes both the pointer and MMIO access. */
        consumer = R16(0x10058);
        producer = R16(0x10068);
        /* The legacy DMA ring uses equal pointers for a full capacity,
         * not zero bytes. Preserve its strict comparison (FR_e6d62).
         * Treating startup equal pointers as empty prevents the first feed. */
        used = producer > consumer ? producer - consumer
                                   : producer + R32(0xa2604) - consumer;
        if (R32(0xa260c) < used) {
            W32(0x1078, R32(0xa2608));
            W32(0x1068, 0x01000000u);
        } else {
            W8(0xa25f6, 2);
        }
    }

    /* DMA-space event: disarm its interrupt until another wait is needed. */
    if (events & 8u) {
        W32(0xa2404, R32(0xa2404) & ~8u);
        W32(0x40, R32(0x40) & ~8u);
        W8(0xa2850, 3);
        events |= 0x40000u;
    }

    if (events & 0x40000u) {
        output = R8(0xa2850);
        if (output == 0u) {
            W8(0xa2850, 1);
            W32(0x5004, 0x01000000u);
            output = 1;
        }
        ready = R8(0xa2823);
        if (output == 1u && ready != 0u) {
            W8(0xa2850, 2);
            output = 2;
        }
        if (output == 2u && ready == 1u) {
            /* A non-raw class must never silently emit a remuxed packet. */
            if (R8(0xa28af) != 0u && R8(0xa28af) != 4u) return result;
            W8(0xa282d, 3);
            /* Main-core FR_802d7f3e watches this scheduling counter. Keep
             * real progress observable, including a queued stop marker. */
            W32(0xa284c, R32(0xa284c) + 1u);
            W32(0xa3918, R32(0xa284c));
            W8(0xa282c, R8(0xa28af) == 0u ? 1u : 0u);
            W32(0x200c, R8(0xa28af) == 0u ? 0x01000000u : 0u);
            W32(0x20, 8);
            if (R8(0xa28af) == 0u) {
                time = R32(0xa2804);
                if (R8(0xa283c) != 0u) {
                    W32(0xa2834, R32(0xa2950));
                    W8(0xa283c, 0);
                    W8(0xa2824, 0);
                } else {
                    delta = R8(0xa2824) == 0u
                          ? (time - R32(0xa2838)) & 0x3fffffffu
                          : R32(0xa2860);
                    W8(0xa2824, 0);
                    W32(0xa2834, R32(0xa2834) + delta);
                }
                W32(0xa2808, time + R32(0xa2860));
                W32(0xa2830, time);
                W32(0xa2838, time);
                W32(0xa3914, time);
            }
            if (!(R32(0x20) & 8u)) {
                W8(0xa2850, 5);
                W32(0xa2404, R32(0xa2404) | 8u);
                W32(0x40, R32(0x40) | 8u);
                return result;
            }
            W8(0xa2850, 3);
            output = 3;
        }
        if (output == 3u && R8(0xa2823) == 1u) {
            /* Preserve USB media framing: write only the 4-byte timestamp,
             * never any byte of the following 188-byte broadcast packet. */
            if (R8(0xa28af) == 0u) {
                packet = (u32)R16(0x10078) + R32(0xa2600);
                W32(packet, TR_OUTPUT_PACKET_PREFIX(packet, R32(0xa2854) | (R32(0xa2834) & R32(0xa2858))));
                W32(0x2004, (u32)R8(0xa28b2) - 1u);
                if (R8(0xa28b3) == 0u) {
                    W32(0x2000, 0x0d000041u);
                } else {
                    /* Original e7a2e..e7a4e submits two commands. The
                     * decompiler omitted the clear-prefix command: verify
                     * volatile MMIO ordering against actual instructions. */
                    W32(0x2000, 0x0c000041u);
                    W32(0x2004, (u32)R8(0xa28b3) - 1u);
                    W32(0x2000, R32(0xa26b4) | 0x05000041u);
                }
                W32(0xa2848, R32(0xa2848) + 1u);
                if (R8(0xa294c) == 1u) {
                    W8(0xa294c, 2);
                    W16(0x5028, 0);
                    W32(0x5030, R32(0xa2834) & R32(0xa2858));
                    W16(0x5024, 1);
                }
            } else if (R8(0xa28af) == 4u) {
                W32(0x2004, 191);
                W32(0x2000, 0x01000041u);
            } else {
                return result;
            }
            if (R8(0xa28b1) == 0u) {
                W8(0xa2823, 0);
                W8(0xa2850, 1);
                result |= 0x20000u;
            } else {
                W8(0xa2823, 2);
                W8(0xa2850, 2);
                result |= 0x40000u;
            }
        }
        /* The generic stop marker closes this single source. Unlike the
         * former remux scheduler, no disabled ES sources have to be awaited. */
        if (R8(0xa2823) == 2u && R8(0xa2420) != 4u) {
            W8(0xa2850, 6);
            W32(0x20, 1);
            if (!(R32(0x2000) & 7u)) events |= 4u;
        }
    }
    if ((events & 4u) && R8(0xa2850) == 6u) {
        if (R32(0x2000) & 7u) return result;
        W32(0xa2404, R32(0xa2404) | 1u);
        W32(0x40, R32(0x40) | 1u);
    }
    if ((events & 1u) && R8(0xa2850) == 6u) {
        W8(0xa2420, 3);
        W8(0xa2850, 7);
        W32(0xa2404, R32(0xa2404) & ~1u);
        W32(0x40, R32(0x40) & ~1u);
        W32(0xa3910, R32(0xa2848));
        W16(0xc0, 0x20);
        result |= 0x80000u;
    }
    return result;
}
