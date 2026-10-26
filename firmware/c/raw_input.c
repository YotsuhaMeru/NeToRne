#include "raw_input.h"

typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;
typedef char input_word_is_four[(sizeof(u32) == 4) ? 1 : -1];
typedef char input_halfword_is_two[(sizeof(u16) == 2) ? 1 : -1];
#ifdef TR_INPUT_TEST
#define R8(a) tr_input_read8(a)
#define R16(a) tr_input_read16(a)
#define R32(a) tr_input_read32(a)
#define W8(a,v) tr_input_write8(a,(u8)(v))
#define W16(a,v) tr_input_write16(a,(u16)(v))
#define W32(a,v) tr_input_write32(a,(u32)(v))
#else
#define R8(a) (*(volatile u8 *)(a))
#define R16(a) (*(volatile u16 *)(a))
#define R32(a) (*(volatile u32 *)(a))
#define W8(a,v) (*(volatile u8 *)(a) = (u8)(v))
#define W16(a,v) (*(volatile u16 *)(a) = (u16)(v))
#define W32(a,v) (*(volatile u32 *)(a) = (u32)(v))
#endif

/* FR DIV0U + 32 DIV1 yields all-one quotient for divisor zero.
 * Keep the native FR result without undefined host C division. */
static u32 divide(u32 numerator, u32 denominator)
{
    u32 remainder = 0, carry, bit, steps = 32;
    /* Restoring unsigned division also gives all ones for divisor zero.
     * Avoid a libgcc call and keep this implementation self-contained. */
    while (steps--) {
        carry = remainder >> 31;
        bit = numerator >> 31;
        remainder = (remainder << 1) | bit;
        numerator <<= 1;
        if (carry || remainder >= denominator) {
            remainder -= denominator;
            numerator |= 1u;
        }
    }
    return numerator;
}

/* Generic clock metadata, e4b66..e4e9e. The flag is a byte (a25bc).
 * The original state1->2 path retains metadata-ready; only explicitly
 * consumed paths clear a25a6. Do not infer this from state changes. */
u32 tr_input_clock(void)
{
    u32 state = R8(0xa25a5), flags, reference, result = 0, consume = 0;
    u32 time, counter, base, slope;
    if (state == 2u || R8(0xa25a6) != 1u) return 0;
    time = R32(0xa25b0);
    counter = R32(0xa25b4);
    base = R32(0xa25b8);
    flags = R8(0xa25bc);
    reference = (flags & 0x40u) ? 0u : flags & 0x80u;
    if (flags & 4u) {
        W8(0xa25a4, 5);
        W8(0xa25a5, state == 3u ? 4u : 5u);
        W8(0xa25f5, 4);
        W8(0xa2928, 1);
        consume = 1;
        result = 0x80000u;
    } else if (state == 0u) {
        W32(0xa25c0, time);
        W32(0xa25c4, counter);
        W32(0xa25e4, base);
        W8(0xa25f0, 0);
        W32(0xa25c8, 0);
        W8(0xa25a5, 1);
        consume = 1;
    } else if (state == 1u) {
        if (reference) {
            W32(0xa25d8, R32(0xa25c0) * 600u);
            W32(0xa2628, R32(0xa262c) + R32(0xa25c0));
            if (R8(0xa25f7) == 0u) {
                slope = divide((time - R32(0xa25c0)) * 600u,
                               counter - R32(0xa25c4));
                W32(0xa25e0, slope);
                W32(0xa25dc, R32(0xa25d8) - (R32(0xa25c4) - R32(0xa25d0)) * slope);
            } else {
                W32(0xa25e8, R32(0xa25e4));
                W32(0xa25ec, R32(0xa25e4));
            }
            W32(0xa25c8, 1);
            W8(0xa25a5, 2);
        } else if (R8(0xa2823) != 3u) {
            if (R8(0xa25f0) == 0u) {
                W32(0xa25c0, time);
                W32(0xa25c4, counter);
                W32(0xa25e4, base);
                W8(0xa25f0, flags & 0x40u);
                W32(0xa25c8, 0);
                W8(0xa2928, 1);
                consume = 1;
                result = 0x80000u;
            } else {
                W8(0xa2620, 1);
                W8(0xa25f0, 0);
            }
        }
    } else if (state == 3u) {
        W32(0xa25c0, time);
        W32(0xa25c4, counter);
        W32(0xa25e4, base);
        W8(0xa25f0, flags & 0x40u);
        W32(0xa25c8, reference);
        if (reference) {
            W32(0xa25d8, time * 600u);
            if (R8(0xa25f7) == 0u) {
                W32(0xa25e0, divide((time - R32(0xa25cc)) * 600u,
                                   counter - R32(0xa25d0)));
            } else {
                W32(0xa25ec, R32(0xa25e8));
            }
        } else if (R8(0xa25f7) != 0u) {
            W32(0xa25d8, ((R32(0xa25ec) << 2) + (counter << 2)) >> 2);
        }
        W8(0xa25a5, 2);
        consume = 1;
    }
    if (consume) W8(0xa25a6, 0);
    return result;
}

static void apply_pending_pids(u32 pid)
{
    u32 pending = R16(0xa2610);
    if (pending && pid == pending) {
        if (pending != R16(0xa28ce)) {
            W16(0xa28ce, pending);
            W8(0xa28d3, 1);
            W8(0xa28d4, 1);
            W8(0xa28d6, 0);
            W16(0xa2610, 0);
            W16(0xa261a, 0);
        } else {
            W8(0xa28d3, 1);
            W16(0xa2610, 0);
        }
    }
    pending = R16(0xa2612);
    if (pending && pid == pending) {
        W16(0xa2916, pending);
        W16(0xa2918, pending);
        if (R16(0xa2864) == pending) W8(0xa291b, R8(0xa2717));
        W16(0xa2612, 0);
    }
    pending = R16(0xa2614);
    if (pending && pid == pending) {
        W32(0xa2894, pending);
        W16(0xa2614, 0);
    }
    pending = R16(0xa2616);
    if (pending && pid == pending) {
        W16(0xa28ac, pending);
        W16(0xa2616, 0);
    }
}

static u32 packet_time(u32 raw)
{
    if (R8(0xa25f7) != 0u) return ((R32(0xa25ec) << 2) + (raw << 2)) >> 2;
    if (R8(0xa25a5) == 2u && R32(0xa25c8))
        return R32(0xa25e0) * (raw - R32(0xa25d0)) + R32(0xa25dc);
    if (R8(0xa25a5) == 5u) return R8(0xa283c) ? 0u : R32(0xa2808);
    return R32(0xa2930) + R32(0xa2804);
}

static void progress(void)
{
    u32 mode = R8(0xa25f8), producer, consumer, capacity, control, previous;
    u32 distance, previous_distance;
    if (mode != 1u && mode != 2u) return;
    if (R8(0xa25f9) != 10u) {
        W8(0xa25f9, R8(0xa25f9) + 1u);
        return;
    }
    control = R32(0xa2414);
    producer = R32(0x1048);
    capacity = R32(control + 0x14);
    if (producer < 8u) producer += capacity;
    producer -= 8u;
    W32(0xa25fc, producer);
    if (mode == 2u) {
        consumer = R32(0x1038);
        previous = R32(0xa3920);
        distance = producer < consumer ? consumer - producer : consumer + capacity - producer;
        previous_distance = previous < consumer ? consumer - previous : consumer + capacity - previous;
        if (distance < previous_distance) W32(0xa25fc, previous);
    }
    W32(R32(control + 0x38), R32(0xa25fc));
    W8(0xa25f9, 0);
}

static void prepare_packet(u32 packet, u32 header)
{
    u32 adaptation = (header >> 4) & 3u, pid = (header >> 8) & 0x1fffu;
#ifdef TR_INPUT_PRESERVE_HEADER
    u32 cipher = 0, marker = 0, prefix;
    /* This explicitly selected wire format always media-encrypts non-NULL
     * payload. The host selects the inverse by firmware identity, never by
     * changing/guessing broadcast scrambling bits. Card processing remains
     * upstream and unchanged. */
    if (adaptation != 2u && pid != 0x1fffu) {
#else
    u32 enabled = R8(0xa26a0), cipher = 0, marker = 0, prefix;
    if (adaptation != 2u && pid != 0x1fffu &&
        (enabled == 2u || (enabled == 1u && (header & 0xc0u)))) {
#endif
        cipher = R32(0xa26b8);
        marker = 0x40u;
    }
    W32(0xa26b4, cipher);
    W32(0xa26d0, marker);
#ifndef TR_INPUT_PRESERVE_HEADER
    W32(packet + 4u, (header & 0xffffff3fu) | marker);
#endif
    if (R8(packet + 4u) == 0xffu && R8(packet + 5u) == 3u && R8(packet + 6u) == 0xffu) {
        W8(0xa28af, 4);
        W8(0xa28b1, 1);
        W8(0xa25f4, 4);
        W8(0xa25f6, 3);
    } else {
        prefix = adaptation == 2u ? 192u : 8u;
#ifdef TR_INPUT_PRESERVE_HEADER
        if (pid == 0x1fffu) prefix = 192u;
#endif
        if (adaptation == 3u
#ifdef TR_INPUT_PRESERVE_HEADER
            && pid != 0x1fffu
#endif
        ) {
            prefix = R8(packet + 8u);
            if (prefix > 183u) prefix = 183u;
            prefix += 9u;
        }
        W8(0xa28af, 0);
        W8(0xa28b2, prefix);
        W8(0xa28b3, 192u - prefix);
    }
}

/* Complete generic-only reader replaces FR_e44fc. Dedicated ES readers,
 * PSI/PCR classification and reassembly are intentionally absent from this
 * path. Existing image7/card/clock notifications and initialization remain.
 * No payload writes, synthetic NULL, allocation, key access or IRQ takeover. */
u32 __attribute__((section(".raw_input"))) tr_raw_input(u32 events)
{
    u32 result = 0, state, packet, header, time;
    if (events & 0x20u) {
        W8(0xa25f4, 2);
        W32(0xa2404, R32(0xa2404) & ~0x20u);
        W32(0x40, R32(0x40) & ~0x20u);
    }
    state = R8(0xa2850);
    if (state != 0u && state != 1u) return 0;
    result |= tr_input_clock();
    if (R8(0xa2823) == 0u) {
        if (R8(0xa390c) & 1u) {
            W8(0xa25f4, 4);
            W8(0xa25f6, 3);
            W8(0xa2823, 2);
            W32(0xa2404, R32(0xa2404) & ~0x20u);
            W32(0x40, R32(0x40) & ~0x20u);
            W32(0x1068, 0x03000000u);
        }
        if (R8(0xa25f4) == 3u) {
            if (R32(0x20) & 0x20u) {
                W8(0xa25f4, 2);
            } else {
                W8(0xa25f4, 1);
                W32(0xa2404, R32(0xa2404) | 0x20u);
                W32(0x40, R32(0x40) | 0x20u);
            }
        }
        if (R8(0xa2620) && R8(0xa2823) == 0u) {
            /* A clock transition is not a packet. Resume this single input
             * without requiring disabled dedicated ES queues to rendezvous.
             * A pending stop above must retain ready=2 for output drain. */
            W8(0xa2823, 0);
            W8(0xa2620, 0);
            W8(0xa2824, 1);
            result |= 0x20000u;
        } else if (R8(0xa25f4) == 2u && R8(0xa25f5) != 1u) {
            state = R8(0xa25a5);
            if (state == 2u || state == 4u || state == 5u) {
                packet = (u32)R16(0x10078) + R32(0xa2600);
                W8(0xa25f4, 3);
                W8(0xa2823, 1);
                time = R32(packet);
                header = R32(packet + 4u);
                apply_pending_pids((header >> 8) & 0x1fffu);
                time = packet_time(time);
                prepare_packet(packet, header);
                W32(0xa2804, time);
                progress();
            }
        }
    }
    if (R8(0xa2823) == 1u || R8(0xa2823) == 2u) result |= 0x40000u;
    return result;
}
