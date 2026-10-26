/* Complete state0 admission/preparation/clock leaf, original e12ee..e1524.
 * Input settings and FIFO metadata are stable during the call.
 * Return the original continuation PC to a reviewed tail-transfer epilogue.
 * TS bytes, packet counter and B-CAS code are untouched. */
typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;
typedef char upstream_word_is_four[(sizeof(u32) == 4) ? 1 : -1];
#define R8(a) ({ u32 v; u32 p = (a); __asm__ __volatile__("ldub @%1,%0" : "=r"(v) : "r"(p) : "memory"); v; })
#define R16(a) ({ u32 v; u32 p = (a); __asm__ __volatile__("lduh @%1,%0" : "=r"(v) : "r"(p) : "memory"); v; })
#define R32(a) (*(volatile u32 *)(a))
#define W8(a,v) (*(volatile u8 *)(a) = (u8)(v))
#define W16(a,v) (*(volatile u16 *)(a) = (u16)(v))
#define W32(a,v) (*(volatile u32 *)(a) = (u32)(v))
#define F32(off) ({ u32 v; __asm__ __volatile__("ld @(r14," #off "),%0" : "=r"(v) : : "memory"); v; })
#define WF32(off,value) do { u32 v = (value); __asm__ __volatile__("st %0,@(r14," #off ")" : : "r"(v) : "memory"); } while (0)

u32 tr_raw_upstream(void)
{
    u32 state, previous, observed, position, stride, step, time, reference, shift;
    /* r14 remains the caller's frame. Build and instruction tests reject
     * a compiler frame/prologue that changes it. These are real FR loads
     * and stores, with memory barriers preserving all observable writes. */
    state = F32(-240);
    WF32(-204, 0);
    previous = R16(state + 4u);
    WF32(-192, previous);
    W8(F32(-448), 0);
    W8(0xa25fau, 0);
    W8(F32(-264), 0);
    W8(0xa25f9u, 0);
    W8(0xa25f8u, 0);
    W8(state + 8u, 0x40u);
    W32(state + 16u, 0);
    W8(state + 9u, 0);
    W8(state + 12u, 0);
    position = F32(-188);
    W32(position, R32(position) | 0x100u);
    position = R32(0xa10f8u);
    if (position < 10000u) W32(0xa10f8u, position + 1u);
    W16(0xd61200u, 0);
    W32(0xd60000u, 0x10000u);

    observed = R16(0xd61110u);
    WF32(-468, observed);
    if (observed != previous) {
        if (observed < previous) {
            observed += 384u;
            WF32(-468, observed);
        }
        stride = R8(state + 6u);
        if (previous + stride < observed) {
            step = R8(state + 7u);
            position = previous;
            do {
                W16(state + 4u, position + step);
                position = R16(state + 4u);
            } while (position + stride < observed);
        }
        position = R16(state + 4u);
        W32(0xd0520cu, stride + position - observed);
        W32(0xd05200u, 0x04000100u);
        W16(state + 4u, position + R8(state + 7u));
        position = R16(state + 4u);
        while (position >= 384u) {
            W16(state + 4u, position - 384u);
            position = R16(state + 4u);
        }
        W8(state, 2);
        if (R32(0xa39f4u) & 0x08000000u)
            ((void (*)(u32))0xe0a5eu)(0x08000000u);
        return 0xe439au;
    }

    W16(state + 4u, previous + R8(state + 7u));
    position = R16(state + 4u);
    WF32(-32, position);
    if (position >= 384u) {
        position -= 384u;
        WF32(-460, position);
        W16(state + 4u, position);
    }
    W32(0xa10ecu, 0);
    WF32(-380, 0);
    if (R8(0xa108cu) == 1u) W32(0xa10ecu, R32(0xd6002cu));
    if (R8(0xa108eu) != 0xc0u) return 0xe1524u;
    time = R32(0xd6002cu);
    WF32(-380, time);
    W32(0xa38c8u, (time & 0xffffff00u) | R8(0xa2574u));
    if (R8(state + 21u) == 0u || R8(0xa2574u) != R8(0xa38c7u)) return 0xe1524u;
    reference = R32(0xa38c0u);
    if (R8(0xa2573u) != 0u) {
        reference &= 0x3fffffffu;
        time &= 0x3fffffffu;
        WF32(-368, time);
        shift = 28u;
    } else shift = 30u;
    WF32(-468, time);
    if (time >= reference) {
        WF32(-364, shift);
        WF32(-36, shift);
        if ((time >> shift) != 3u) goto notify;
        WF32(-432, shift);
        if ((reference >> shift) != 0u) goto notify;
    }
    WF32(-40, shift);
    if ((time >> shift) != 0u || (reference >> shift) != 3u) return 0xe1524u;
notify:
    W16(0xd000c0u, 0x400u);
    WF32(-444, 0);
    W8(0xa2541u, 0);
    return 0xe1524u;
}
