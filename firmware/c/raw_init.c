/* Mode 8/9 FR_a08b2 initialization. Retain the hardware reset, legacy
 * aligned zero-fill and transport setup calls. Card operations are elsewhere.
 * Address zero is an actual FR MMIO register, not a host null pointer. */
typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;
#define R32(a) (*(volatile u32 *)(a))
#define R16(a) (*(volatile u16 *)(a))
#define R8(a) (*(volatile u8 *)(a))
#define ZERO(a, n) (((void (*)(u32, u32))0xa0a58)((a), (n)))
#define MMIO_ZERO(v) __asm__ volatile ("st %0,@%1" : : "r"((u32)(v)), "r"(0u) : "memory")

void tr_raw_initialize(void)
{
    u32 address;
    R8(0xa2420) = 0;
    __asm__ volatile ("andccr #0xef" : : : "memory");
    R32(0x10) = 0xffffffffu;
    R32(0x20) = 0xffffffffu;
    R32(0xb8) = 0xffffffffu;
    R32(0xc8) = 0xffffffffu;
    R32(0x90) = 0xffffffffu;
    R32(0x50) = 0x4040ffu;
    R32(0x60) = 0;
    R32(0x30) = 0x4040ffu;
    R32(0x40) = 0xfc000004u;
    R32(0xb4) = 0;
    R32(0xa0) = 0;
    ((void (*)(void))0xe7f9c)();
    MMIO_ZERO(0);
    R32(0x04) = 0xffffffffu;
    ZERO(0xa2bd0, 0x20);
    ZERO(0xa2bf0, 0x410);
    ZERO(0xa3800, 0x20);
    ZERO(0xa3840, 0x38);
    ZERO(0xa3880, 0x54);
    for (address = 0xa3900; address <= 0xa3918; address += 4) R32(address) = 0;
    /* a391c is deliberately not cleared in the original initialization. */
    for (address = 0xa3920; address <= 0xa3940; address += 4) R32(address) = 0;
    ZERO(0xa3a00, 0x20);
    ZERO(0xa3a20, 0x20);
    ZERO(0xa3a40, 0x20);
    ZERO(0xa3980, 0x14);
    MMIO_ZERO(0x7f);
    MMIO_ZERO(0);
    ((void (*)(void))0xa09c8)();
    R16(0xc0) = 1;
}
