/* Mode 8/9 transport setup and input-ring configuration.
 * Original callable entries and reset/clock/media helpers are retained.
 * All addresses below are FR CPU-local MMIO/RAM, never USB addresses. */
typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;
#define R32(a) (*(volatile u32 *)(a))
#define R16(a) (*(volatile u16 *)(a))
#define R8(a) (*(volatile u8 *)(a))
#define ZERO(a, n) (((void (*)(u32, u32))0xa0a58)((a), (n)))
#define S32(o) R32(settings + (o))
#define S8(o) R8(settings + (o))
#define P16(o) R16(pids + (o))
#define P8(o) R8(pids + (o))
#define MMIO_ZERO(v) __asm__ volatile ("st %0,@%1" : : "r"((u32)(v)), "r"(0u) : "memory")

void tr_raw_ring_configure(void);

void __attribute__((section(".control_setup"))) tr_raw_setup(void)
{
    R8(0xa2420) = 0;
    R32(4) = 0xffffffffu;
    R32(0xa2408) = 0;
    R32(0xa2400) = 0;
    R32(0xa2404) = 0xfc000004u;
    R32(0xa241c) = 0xa3800;
    R32(0xa2418) = 0xa3840;
    R32(0xa2414) = 0xa3880;
    R32(0xa2410) = 0xa3980;
    R32(0xa240c) = 0xf000a0;
    MMIO_ZERO(0);
    R32(4) = 0xffffffffu;
    tr_raw_ring_configure();
    ((void (*)(void))0xa12d4)();
    ((void (*)(void))0xa1520)();
    R32(0x10) = 0xffffffffu;
    R32(0x20) = 0xffffffffu;
    R32(0xb8) = 0xffffffffu;
    R32(4) = 0xffffffffu;
    R8(0xa2420) = 1;
}

static const u8 invalid_counters[] __attribute__((section(".control_constants"))) = {
    0x00,0x04,0x08,0x38,0x24,0x28,0x2c,0x3c
};
static const u8 clock_zeros[] __attribute__((section(".control_constants"))) = {
    0xc0,0xc4,0xc8,0xd8,0xcc,0xd0,0xd4,0xdc,0xe0,0xe4,0xe8,0xec
};

void __attribute__((section(".control_ring"))) tr_raw_ring_configure(void)
{
    u32 i, settings, pids;
    ZERO(0xa265c,0x40);
    ZERO(0xa2634,0x28);
    ZERO(0xa25f4,0x40);
    ZERO(0xa25a4,0x50);
    ZERO(0xa2574,0x30);
    ZERO(0xa2424,0x150);
    /* The configuration pointers are outside all cleared extents and the
     * caller holds configuration stable while transport is initialized. */
    settings=R32(0xa2414); pids=R32(0xa2418);
    R8(0xa269d) = 0;
    R8(0xa269c) = 0;
    for (i=0;i<8;i++) R32(0xa3900+invalid_counters[i]) = 0xffffffffu;
    R32(0xa2660)=0; R32(0xa2664)=0;
    R32(0xa2668)=S32(0)&0x07ffffe0u;
    R32(0xa266c)=S32(4)&0x00ffffe0u;
    R32(0xa2638)=0; R32(0xa263c)=0;
    R32(0xa2640)=S32(8)&0x07ffffe0u;
    R32(0xa2644)=S32(12)&0x00ffffe0u;
    R8(0xa25f7)=P8(0)>>7;
    R8(0xa25f8)=S8(0x34)&3;
    R8(0xa25f9)=0; R32(0xa25fc)=0;
    for (i=0xa2610;i<=0xa261e;i+=2) R16(i)=0;
    R8(0xa2620)=0;
    R32(0xa2624)=0; R32(0xa2628)=0; R32(0xa2630)=0;
    R32(0xa390c)=0; R32(0xa3940)=0;
    R32(0xa25a8)=0; R32(0xa25ac)=0;
    for (i=0;i<12;i++) R32(0xa2500+clock_zeros[i])=0;
    R32(0xa2578)=0; R32(0xa257c)=0;
    R32(0xa2580)=S32(0x3c)&0x07ffffe0u;
    R32(0xa2584)=S32(0x40)&0x00ffffe0u;
    R32(0xa2564)=0; R32(0xa2568)=0; R32(0xa256c)=0; R32(0xa2570)=0;
    R32(0x1010)=S32(0)&0x07ffffe0u;
    R32(0x1020)=S32(4)&0x00ffffe0u;
    R32(0x1014)=S32(8)&0x07ffffe0u;
    R32(0x1024)=S32(12)&0x00ffffe0u;
    R32(0x1018)=S32(16)&0x07ffffe0u;
    R32(0x1028)=S32(20)&0x00ffffe0u;
    R32(0x101c)=S32(0x3c)&0x07ffffe0u;
    R32(0x102c)=S32(0x40)&0x00ffffe0u;
    R32(0x1000)=0xfff00000u;
    R16(0x10010)=0; R16(0x10030)=0; R16(0x10034)=0x800;
    if ((P16(12)&0x1fff)==0) {
        R16(0x10038)=0x1000; R16(0x1003c)=0x1840; R16(0x10040)=0x1840;
        R32(0xa2600)=0x13000;
    } else {
        R16(0x10038)=0xc00; R16(0x1003c)=0x1440; R16(0x10040)=0x1840;
        R32(0xa2600)=0x12c00;
    }
    R32(0xa2604)=0x840; R32(0xa260c)=0x470; R32(0xa2608)=0x460;
    R16(0x100b0)=0x1880; R16(0x100b4)=0x1900;
    R16(0x10080)=0xb8; R16(0x10084)=0xb8; R16(0x10088)=0xc0; R16(0x1008c)=0xb8;
    R8(0xa265c)=(P16(8)&0x1fff)?0:5; R8(0xa265d)=0;
    R8(0xa2634)=(P16(10)&0x1fff)?0:5; R8(0xa2635)=0;
    if ((P8(0)&0x30)==0) { R8(0xa25f4)=4; R8(0xa25f6)=3; }
    else { R8(0xa25f4)=0; R8(0xa25f6)=0; }
    if ((P8(0)&0x30)==0x20 || (P8(0)&0x30)==0x30) {
        R8(0xa25f5)=0;
        R32(0xa262c)=((P8(0)&0x30)==0x20)?0xb000:0x3000;
    } else R8(0xa25f5)=4;
    R8(0xa25a4)=(P16(0x24)&0x1fff)?0:5;
    R8(0xa25a5)=R8(0xa25a4); R8(0xa25a6)=0;
    R8(0xa2574)=(P16(12)&0x1fff)?0:5; R8(0xa2575)=0;
}
