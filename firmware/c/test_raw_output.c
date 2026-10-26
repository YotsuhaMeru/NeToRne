#include "raw_output.h"
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned char memory[0xb0000];
static unsigned int writes, submits, timestamp_writes, dma_space;
static unsigned int packet_address;
static unsigned int submit_class[16], submit_count[16];

unsigned char tr_output_read8(unsigned int a)
{ assert(a < sizeof memory); return memory[a]; }
unsigned short tr_output_read16(unsigned int a)
{
    assert(a == 0x10058 || a == 0x10068 || a == 0x10078 ||
           a == 0x5020 || a == 0x5024 || a == 0x5028 || a == 0xc0);
    return (unsigned short)((unsigned int)memory[a] << 8 | memory[a+1]);
}
unsigned int tr_output_read32(unsigned int a)
{
    assert(a + 4 <= sizeof memory && (a & 3) == 0);
    assert(a != 0x10058 && a != 0x10068 && a != 0x10078);
    if (a == 0x20) return dma_space ? 8u : 0u;
    return ((unsigned int)memory[a] << 24) | ((unsigned int)memory[a+1] << 16)
         | ((unsigned int)memory[a+2] << 8) | memory[a+3];
}
static void put32(unsigned int a, unsigned int v)
{
    memory[a] = (unsigned char)(v >> 24); memory[a+1] = (unsigned char)(v >> 16);
    memory[a+2] = (unsigned char)(v >> 8); memory[a+3] = (unsigned char)v;
}
static void pointer(unsigned int a, unsigned int v)
{
    memory[a] = (unsigned char)(v >> 8); memory[a+1] = (unsigned char)v;
    /* Poison adjacent halfwords so a mistaken 32-bit load cannot pass. */
    memory[a+2] = 0xa5; memory[a+3] = 0x5a;
}
void tr_output_write8(unsigned int a, unsigned char v)
{ assert(a >= 0xa2400 && a < 0xa4000); ++writes; memory[a] = v; }
void tr_output_write16(unsigned int a, unsigned short v)
{
    assert(a == 0x5020 || a == 0x5024 || a == 0x5028 || a == 0xc0);
    ++writes;
    memory[a] = (unsigned char)(v >> 8); memory[a+1] = (unsigned char)v;
    assert(memory[a+2] == 0xa5 && memory[a+3] == 0x5a);
}
void tr_output_write32(unsigned int a, unsigned int v)
{
    assert(a + 4 <= sizeof memory && (a & 3) == 0);
    assert(a != 0x5020 && a != 0x5024 && a != 0x5028 && a != 0xc0);
    /* The timestamp is the only permitted write near the broadcast TS. */
    if (a >= 0x12c00 && a < 0x13840) {
        assert(a == packet_address);
        ++timestamp_writes;
    }
    if (a == 0x2000) {
        assert(submits < 16);
        submit_class[submits] = v;
        submit_count[submits++] = tr_output_read32(0x2004);
    }
    ++writes;
    if (a != 0x20) put32(a, v);
}
static void reset(void)
{
    memset(memory, 0, sizeof memory);
    writes = submits = timestamp_writes = 0; dma_space = 1;
    packet_address = 0x13000;
    put32(0xa2404, 0xfc000004); put32(0x40, 0xfc000004);
    put32(0xa2600, 0x13000); put32(0xa2604, 0x840);
    put32(0xa2608, 0x460); put32(0xa260c, 0x470);
    put32(0xa2858, 0x3fffffff); put32(0xa2854, 0x80000000);
    put32(0xa2860, 100); put32(0xa2950, 500);
    pointer(0x10058, 0); pointer(0x10068, 0); pointer(0x10078, 0);
    pointer(0x5020, 0); pointer(0x5024, 0); pointer(0x5028, 0); pointer(0xc0, 0);
    memory[0xa2420] = 2; memory[0xa283c] = 1;
}
static void ready_at(unsigned int time, unsigned int prefix, unsigned int tail,
                     unsigned int base, unsigned int offset)
{
    unsigned int i;
    put32(0xa2600, base); pointer(0x10078, offset);
    packet_address = base + offset;
    for (i = 0; i < 192; ++i) memory[packet_address+i] = (unsigned char)(i * 73u + time);
    memory[packet_address+4] = 0x47;
    memory[0xa2823] = 1; memory[0xa28af] = 0; memory[0xa28b1] = 0;
    memory[0xa28b2] = (unsigned char)prefix; memory[0xa28b3] = (unsigned char)tail;
    put32(0xa2804, time);
}
static void ready_packet(unsigned int time, unsigned int prefix, unsigned int tail)
{ ready_at(time, prefix, tail, 0x13000, 0); }
static unsigned int expected_prefix(unsigned int address)
{
#ifdef TR_OUTPUT_PREFIX_CRC
    unsigned int crc = 0xffffffffu, i, bit;
    for (i = 0; i < 188; ++i) {
        crc ^= memory[address+4+i];
        for (bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ ((crc & 1u) ? 0xedb88320u : 0u);
    }
    return crc ^ 0xffffffffu;
#else
    (void)address;
    return 0x800001f4u;
#endif
}
int main(void)
{
    unsigned char before[188];
    unsigned int result, count, i;
    reset();
    ready_packet(1000, 8, 184);
    memcpy(before, memory + 0x13004, sizeof before);
    result = tr_raw_output(0x40000);
    assert(result == 0x20000 && submits == 2);
    assert(submit_class[0] == 0x0c000041 && submit_count[0] == 7);
    assert(submit_class[1] == 0x05000041 && submit_count[1] == 183);
    assert(submit_count[0] + submit_count[1] + 2 == 192);
    assert(tr_output_read32(0x13000) == expected_prefix(0x13000));
    assert(tr_output_read32(0xa2848) == 1 && memory[0xa2823] == 0);
    assert(memcmp(before, memory + 0x13004, sizeof before) == 0);

    /* Adaptation-only packets transfer the full packet with no cipher tail. */
    ready_packet(1200, 192, 0);
    result = tr_raw_output(0x40000);
    assert(result == 0x20000 && submits == 3);
    assert(submit_class[2] == 0x0d000041 && submit_count[2] == 191);
    assert(tr_output_read32(0xa2834) == 700);

    /* A full DMA must not release the borrowed packet or consume twice. */
    ready_packet(1300, 17, 175);
    memcpy(before, memory + 0x13004, sizeof before);
    dma_space = 0;
    count = submits;
    assert(tr_raw_output(0x40000) == 0 && submits == count);
    assert(memory[0xa2850] == 5 && memory[0xa2823] == 1);
    assert((tr_output_read32(0xa2404) & 8) && (tr_output_read32(0x40) & 8));
    for (i = 0; i < 4; ++i) assert(tr_raw_output(0x40000) == 0 && submits == count);
    dma_space = 1;
    assert(tr_raw_output(8) == 0x20000 && submits == count + 2);
    assert(submit_class[count] == 0x0c000041 && submit_count[count] == 16);
    assert(submit_class[count+1] == 0x05000041 && submit_count[count+1] == 174);
    assert(submit_count[count] + submit_count[count+1] + 2 == 192);
    assert(tr_output_read32(0xa2834) == 800);
    assert(memcmp(before, memory + 0x13004, sizeof before) == 0);
    assert(!(tr_output_read32(0xa2404) & 8));

    /* The input ring reserve check works across wrap and at the boundary. */
    memory[0xa25f6] = 1;
    pointer(0x10058, 0x800); pointer(0x10068, 0x500);
    tr_raw_output(0x20000000);
    assert(tr_output_read32(0x1078) == 0x460 && tr_output_read32(0x1068) == 0x1000000);
    pointer(0x10058, 0); pointer(0x10068, 0x470);
    tr_raw_output(0x20000000);
    assert(memory[0xa25f6] == 2);
    pointer(0x10068, 0x471); put32(0x1078, 0);
    tr_raw_output(4);
    assert(memory[0xa25f6] == 1 && tr_output_read32(0x1078) == 0x460);

    /* Legacy DMA equal-pointer encoding is full; it must start a feed. */
    memory[0xa25f6] = 1;
    pointer(0x10058, 0); pointer(0x10068, 0); put32(0x1078, 0);
    tr_raw_output(0x20000000);
    assert(memory[0xa25f6] == 1 && tr_output_read32(0x1078) == 0x460);
    memory[0xa25f6] = 1;
    pointer(0x10058, 0x810); pointer(0x10068, 0x810); put32(0x1078, 0);
    tr_raw_output(0x20000000);
    assert(memory[0xa25f6] == 1 && tr_output_read32(0x1078) == 0x460);

    /* No synthetic NULL packets when the former NULL-insertion flags are set. */
    memory[0xa2420] = 4; memory[0xa2851] = 1; memory[0xa2852] = 1;
    count = submits;
    tr_raw_output(0x40000);
    assert(submits == count);

    /* Drain stop marker, wait for DMA completion, then notify the main core.
     * Disabled ES states deliberately stay 0 to prove no dependency on them. */
    memory[0xa2420] = 2; memory[0xa2850] = 1;
    memory[0xa2823] = 1; memory[0xa28af] = 4; memory[0xa28b1] = 1;
    result = tr_raw_output(0x40000);
    assert(result == 0x40000 && memory[0xa2850] == 6);
    assert(submit_class[submits-1] == 0x01000041 && submit_count[submits-1] == 191);
    assert(tr_output_read32(0xa2848) == 3 && timestamp_writes == 3);
    assert(tr_raw_output(4) == 0 && !(tr_output_read32(0xa2404) & 1));
    put32(0x2000, 0); tr_raw_output(4);
    assert(tr_output_read32(0xa2404) & 1);
    assert(tr_raw_output(1) == 0x80000);
    assert(memory[0xa2420] == 3 && memory[0xa2850] == 7);
    assert(tr_output_read16(0xc0) == 0x20 && tr_output_read32(0xa3910) == 3);
    assert(!(tr_output_read32(0xa2404) & 1));

    /* Both legacy buffer bases and nonzero halfword pointer offsets.
     * Adjacent register bytes stay poisoned; no TS byte may be written. */
    for (i = 0; i < 6; ++i) {
        unsigned int offsets[3] = {0, 0x200, 0x700};
        reset();
        ready_at(1000, 17, 175, i < 3 ? 0x12c00 : 0x13000, offsets[i % 3]);
        memcpy(before, memory + packet_address + 4, sizeof before);
        assert(tr_raw_output(0x40000) == 0x20000);
        assert(timestamp_writes == 1 && submits == 2);
        assert(tr_output_read32(packet_address) == expected_prefix(packet_address));
        assert(memcmp(before, memory + packet_address + 4, sizeof before) == 0);
        assert(submit_class[0] == 0x0c000041 && submit_count[0] == 16);
        assert(submit_class[1] == 0x05000041 && submit_count[1] == 174);
    }
    /* Clock control and completion notification are halfword MMIO writes;
     * 32-bit accesses are rejected even when the stored numeric value fits. */
    reset();
    memory[0xa294c] = 1;
    ready_packet(1000, 8, 184);
    assert(tr_raw_output(0x40000) == 0x20000 && memory[0xa294c] == 2);
    assert(tr_output_read16(0x5028) == 0 && tr_output_read16(0x5024) == 1);
    assert(tr_output_read32(0x5030) == 500);
    put32(0x5000, 0x40000000);
    tr_raw_output(4);
    assert(memory[0xa294c] == 3 && tr_output_read16(0x5020) == 0x101);
    memory[0xa294c] = 2; put32(0x5000, 0);
    tr_raw_output(4);
    assert(tr_output_read16(0x5020) == 1);
    puts("C raw output: DMA reserve, busy retry, unchanged TS, marker drain and stop passed");
    return 0;
}
