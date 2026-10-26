#include "raw_input.h"
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned char memory[0xb0000];
static unsigned int packet_address = 0x13000, input_status;
static unsigned int write_count, addresses[128], widths[128];
static void put32(unsigned int a, unsigned int v)
{
    memory[a] = (unsigned char)(v >> 24); memory[a+1] = (unsigned char)(v >> 16);
    memory[a+2] = (unsigned char)(v >> 8); memory[a+3] = (unsigned char)v;
}
static void put16(unsigned int a, unsigned int v)
{ memory[a] = (unsigned char)(v >> 8); memory[a+1] = (unsigned char)v; }
unsigned char tr_input_read8(unsigned int a)
{ assert(a < sizeof memory); return memory[a]; }
unsigned short tr_input_read16(unsigned int a)
{
    assert(!(a & 1u) && (a == 0x10078 || (a >= 0xa2400 && a < 0xa4000)));
    return (unsigned short)((unsigned int)memory[a] << 8 | memory[a+1]);
}
unsigned int tr_input_read32(unsigned int a)
{
    assert(a + 4 <= sizeof memory && !(a & 3u) && a != 0x10078);
    if (a == 0x20) return input_status;
    return (unsigned int)memory[a] << 24 | (unsigned int)memory[a+1] << 16
         | (unsigned int)memory[a+2] << 8 | memory[a+3];
}
static void note(unsigned int a, unsigned int size)
{
    assert(write_count < 128);
    addresses[write_count] = a; widths[write_count++] = size;
    if (a >= packet_address && a < packet_address + 192u)
#ifdef TR_INPUT_PRESERVE_HEADER
        assert(!"Preserved input must not write packet memory");
#else
        assert(a == packet_address + 4u && size == 4u);
#endif
}
void tr_input_write8(unsigned int a, unsigned char v)
{ assert(a >= 0xa2400 && a < 0xa4000); note(a, 1); memory[a] = v; }
void tr_input_write16(unsigned int a, unsigned short v)
{ assert(a >= 0xa2400 && a < 0xa4000 && !(a & 1u)); note(a, 2); put16(a, v); }
void tr_input_write32(unsigned int a, unsigned int v)
{
    assert(a + 4 <= sizeof memory && !(a & 3u) && a != 0x10078);
    assert((a >= 0xa2400 && a < 0xa4000) || a == 0x40 || a == 0x1068 ||
           a == 0x90080 || a == packet_address + 4u);
    note(a, 4); put32(a, v);
}
static void reset(void)
{
    memset(memory + 0xa2400, 0, 0x1c00);
    input_status = 0x20;
    write_count = 0;
    packet_address = 0x13000;
    put16(0x10078, 0); put16(0x1007a, 0xa55a);
    put32(0xa2600, 0x13000);
    put32(0xa2404, 0xfc000024); put32(0x40, 0xfc000024);
    put32(0xa2414, 0x90000); put32(0x90014, 0x1000); put32(0x90038, 0x90080);
    memory[0xa25a5] = 5;
    memory[0xa26a0] = 2;
    put32(0xa26b8, 0x00123400);
    put32(0x1068, 0);
}
static void packet(unsigned int pid, unsigned int af, unsigned int sc, unsigned int length)
{
    unsigned int i;
    for (i = 0; i < 192; ++i) memory[packet_address+i] = (unsigned char)(i*73u+pid*17u+length);
    memory[packet_address+4] = 0x47;
    memory[packet_address+5] = (unsigned char)(0xe0 | (pid >> 8));
    memory[packet_address+6] = (unsigned char)pid;
    memory[packet_address+7] = (unsigned char)((sc << 6) | (af << 4) | (pid & 15));
    memory[packet_address+8] = (unsigned char)length;
}
static void packet_cases(void)
{
    unsigned char before[192];
    unsigned int pid, af, length, mode, sc, expected, prefix;
    for (pid = 0; pid < 8192; ++pid)
    for (af = 0; af < 4; ++af)
    for (length = 0; length < 256; ++length) {
        reset();
        mode = length % 4u; sc = (length / 4u) % 4u;
        memory[0xa26a0] = (unsigned char)mode;
        packet(pid, af, sc, length);
        memcpy(before, memory+packet_address, 192);
        assert(tr_raw_input(0x20) == 0x40000u);
        expected =
#ifdef TR_INPUT_PRESERVE_HEADER
            pid != 8191 && af != 2 ? 0x40u : 0u;
#else
            (mode == 2 || (mode == 1 && sc)) && pid != 8191 && af != 2 ? 0x40u : 0u;
        before[7] = (unsigned char)((before[7] & 0x3fu) | expected);
#endif
        assert(memcmp(before, memory+packet_address, 192) == 0);
        assert(tr_input_read32(0xa26b4) == (expected ? 0x00123400u : 0u));
        assert(tr_input_read32(0xa26d0) == expected);
        assert(memory[0xa25f4] == 3 && memory[0xa2823] == 1 && memory[0xa28af] == 0);
        prefix = af == 2 ? 192u : af == 3 ? (length > 183 ? 183 : length) + 9u : 8u;
#ifdef TR_INPUT_PRESERVE_HEADER
        if (pid == 8191) prefix = 192u;
        assert(write_count && addresses[0] != packet_address);
#endif
        assert(memory[0xa28b2] == prefix && memory[0xa28b3] == 192u-prefix);
        assert(memory[0x1007a] == 0xa5 && memory[0x1007b] == 0x5a);
    }
}
static void clocks(void)
{
    reset(); memory[0xa25a5] = 1; memory[0xa25a6] = 1; memory[0xa25bc] = 0x80;
    put32(0xa25b0, 150); put32(0xa25b4, 10); put32(0xa25c0, 100);
    put32(0xa25c4, 5); put32(0xa25d0, 1); put32(0xa262c, 0xb000);
    assert(tr_input_clock() == 0);
    assert(memory[0xa25a5] == 2 && memory[0xa25a6] == 1);
    assert(tr_input_read32(0xa25e0) == 6000 && tr_input_read32(0xa25dc) == 36000);
    assert(tr_input_read32(0xa2628) == 0xb064 && tr_input_read32(0xa25c8) == 1);
    reset(); memory[0xa25a5] = 1; memory[0xa25a6] = 1; memory[0xa25bc] = 0x80;
    put32(0xa25b0, 150); put32(0xa25b4, 5); put32(0xa25c0, 100); put32(0xa25c4, 5);
    assert(tr_input_clock() == 0 && tr_input_read32(0xa25e0) == 0xffffffffu);
    reset(); memory[0xa25a5] = 1; memory[0xa25a6] = 1; memory[0xa25bc] = 0xc0;
    assert(tr_input_clock() == 0x80000u && memory[0xa25a5] == 1 && memory[0xa25a6] == 0);
    assert(memory[0xa25f0] == 0x40 && tr_input_read32(0xa25c8) == 0);
    reset(); memory[0xa25a5] = 3; memory[0xa25a6] = 1; memory[0xa25bc] = 4;
    assert(tr_input_clock() == 0x80000u && memory[0xa25a4] == 5 && memory[0xa25a5] == 4);
    assert(memory[0xa25f5] == 4 && memory[0xa25a6] == 0);
}
static void states_and_progress(void)
{
    unsigned int i, pid_position = 128, flag_position = 128;
    reset(); packet(0x100, 1, 2, 0); memory[0xa2850] = 3;
    assert(tr_raw_input(0x20) == 0 && memory[0xa25f4] == 2 && memory[0xa2823] == 0);
    reset(); memory[0xa25f4] = 3; input_status = 0;
    assert(tr_raw_input(0) == 0 && memory[0xa25f4] == 1);
    assert(tr_input_read32(0x40) & 0x20u);
    reset(); memory[0xa2620] = 1;
    assert(tr_raw_input(0) == 0x20000u && memory[0xa2823] == 0 && memory[0xa2824] == 1);
    reset(); memory[0xa390c] = 1;
    assert(tr_raw_input(0) == 0x40000u && memory[0xa2823] == 2 && memory[0xa25f6] == 3);
    assert(tr_input_read32(0x1068) == 0x03000000u);
    reset(); memory[0xa390c] = 1; memory[0xa2620] = 1;
    assert(tr_raw_input(0x20) == 0x40000u && memory[0xa2823] == 2);
    assert(memory[0xa2620] == 1 && memory[0xa2824] == 0);
    assert(tr_input_read32(0x1068) == 0x03000000u);
    reset(); packet(0x100, 3, 2, 7); put16(0xa2610, 0x100); put16(0xa261a, 0x200);
    put16(0xa2612, 0x100); put16(0xa2864, 0x100); memory[0xa2717] = 0x82;
    put16(0xa2614, 0x100); put16(0xa2616, 0x100);
    memory[0xa25f8] = 2; memory[0xa25f9] = 10;
    put32(0x1048, 4); put32(0x1038, 0x20); put32(0xa3920, 0x10);
    assert(tr_raw_input(0x20) == 0x40000u);
    assert(tr_input_read32(0xa25fc) == 0xffc && tr_input_read32(0x90080) == 0xffc);
    assert(memory[0xa25f9] == 0 && tr_input_read16(0xa28ce) == 0x100);
    assert(tr_input_read16(0xa2916) == 0x100 && tr_input_read16(0xa2918) == 0x100);
    assert(memory[0xa291b] == 0x82 && tr_input_read32(0xa2894) == 0x100);
    assert(tr_input_read16(0xa28ac) == 0x100 && tr_input_read16(0xa2610) == 0);
    for (i = 0; i < write_count; ++i) {
        if (addresses[i] == 0xa28ce) { assert(widths[i] == 2); pid_position = i; }
        if (addresses[i] == 0xa28d3) { assert(widths[i] == 1); flag_position = i; }
    }
    assert(pid_position < flag_position);
    reset(); packet(0x100, 1, 2, 0); memory[packet_address+4] = 0xff;
    memory[packet_address+5] = 3; memory[packet_address+6] = 0xff;
    assert(tr_raw_input(0x20) == 0x40000u && memory[0xa28af] == 4);
    assert(memory[0xa28b1] == 1 && memory[0xa25f4] == 4 && memory[0xa25f6] == 3);
}
int main(void)
{
    clocks(); states_and_progress(); packet_cases();
    puts("C input: clock/IRQ/PID/stop/progress and 8,388,608 framed packet cases passed");
    return 0;
}
