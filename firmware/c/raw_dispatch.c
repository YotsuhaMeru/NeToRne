/* Mode 8/9 polling dispatcher at FR_e4038. Hardware initialization and
 * command/clock handlers remain callable legacy functions. No card logic.
 * Interrupt windows match the original instructions, including the second
 * mask before the retained control handlers. */
typedef unsigned int u32;
typedef unsigned char u8;
#define R32(a) (*(volatile u32 *)(a))
#define R8(a) (*(volatile u8 *)(a))
#define CALL(a, v) (((u32 (*)(u32))(a))(v))
#define MASK() __asm__ volatile ("andccr #0xef" : : : "memory")
#define UNMASK() __asm__ volatile ("orccr #0x10" : : : "memory")

void tr_raw_dispatch(void)
{
    ((void (*)(void))0xa08b2)();
    for (;;) {
        u32 events, result;
        MASK();
        events = R32(0x20);
        R32(0x20) = events;
        events = (events & R32(0xa2404)) | R32(0xa2400);
        R32(0xa2400) = 0;
        if (events & 0x08000000u) R32(0xa2408) |= 0x08000000u;
        if (events & 0x04000000u) R32(0xa2408) |= 0x04000000u;
        if (R32(0xa2408) & 0x20000u) {
            events |= 0x20000u;
            R32(0xa2408) &= ~0x20000u;
        }
        if (R32(0xa2408) & 0x400000u) {
            events |= 0x400000u;
            R32(0xa2408) &= ~0x400000u;
        }
        if (events & 0x20020u) {
            result = CALL(0xe44fc, events);
            R32(0xa2400) |= result;
        }
        if (events & 0xf04400ddu) {
            result = CALL(0xe6d62, events);
            R32(0xa2400) |= result;
        }
        if (R32(0xa2400) & 0x80000u) {
            events |= 0x80000u;
            R32(0xa2400) &= ~0x80000u;
        }
        if (events & 0x80000u) {
            result = CALL(0xa1396, events);
            R32(0xa2400) |= result;
        }
        /* e4126/e4128 briefly allow pending IRQs before ready-state reads.
         * This window is absent from the decompiler's C output. */
        UNMASK();
        MASK();
        if ((R8(0xa265c) == 1 && R8(0xa265d) == 0) ||
            (R8(0xa2634) == 1 && R8(0xa2635) == 0) ||
            (R8(0xa25a4) == 1 && R8(0xa25a6) == 0) ||
            (R8(0xa2574) == 1 && R8(0xa2575) == 0))
            R32(0xa2408) |= 0x10000u;
        if (R32(0xa2400) & 0x10000u) {
            R32(0xa2408) |= 0x10000u;
            R32(0xa2400) &= ~0x10000u;
        }
        if (R32(0xa2400) == 0 && R32(0xa2408) == 0)
            ((void (*)(u32, u32))0xe401c)(0, 0);
        UNMASK();
        if (R32(0xa2408) == 0) continue;
        MASK();
        /* Retained high handler receives the current pending word; its
         * return is passed directly to a0b0e after publishing that word. */
        events = R32(0xa2408);
        if (events & 0xffffu) {
            events = CALL(0xa0000, events);
            R32(0xa2408) = events;
        }
        if (R32(0xa2408) & 0xffff0000u) {
            events = CALL(0xe42a6, R32(0xa2408));
            R32(0xa2408) = events;
            events = CALL(0xa0b0e, events);
            R32(0xa2408) = events;
        }
    }
}
