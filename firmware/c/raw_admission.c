/* Upstream mode7 pointer alignment, original e136c..e1408.
 * Caller settings are stable during this leaf. No TS byte is rewritten.
 * Return 0 for e1430 (aligned), 1 for e1408 (discard completion).
 * Standalone FR proof only: no firmware entry bridge is installed yet. */
typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;
typedef char admission_word_is_four[(sizeof(u32) == 4) ? 1 : -1];
typedef char admission_half_is_two[(sizeof(u16) == 2) ? 1 : -1];
#define R8(a) (*(volatile u8 *)(a))
#define R16(a) (*(volatile u16 *)(a))
#define R32(a) (*(volatile u32 *)(a))
#define W16(a,v) (*(volatile u16 *)(a) = (u16)(v))
#define W32(a,v) (*(volatile u32 *)(a) = (u32)(v))
#define F32(offset) R32(frame + (u32)(offset))
#define WF32(offset,v) W32(frame + (u32)(offset),v)

u32 tr_raw_admission(u32 frame)
{
    u32 observed = R16(0xd61110u);
    u32 previous = F32(-192), state, stride, step, position, target;
    WF32(-468, observed);
    if (observed == previous) return 0;
    if (observed < previous) {
        observed += 384u;
        WF32(-468, observed);
    }
    target = observed;
    state = F32(-240);
    stride = R8(state + 6u);
    if (previous + stride < target) {
        step = R8(state + 7u);
        position = previous;
        do {
            W16(state + 4u, position + step);
            position = R16(state + 4u);
        } while (position + stride < target);
    }
    position = R16(state + 4u);
    W32(0xd0520cu, stride + position - target);
    W32(0xd05200u, 0x04000100u);
    W16(state + 4u, position + R8(state + 7u));
    position = R16(state + 4u);
    while (position >= 384u) {
        W16(state + 4u, position - 384u);
        position = R16(state + 4u);
    }
    return 1;
}
