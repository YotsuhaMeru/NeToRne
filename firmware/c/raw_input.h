#ifndef TORNE_RAW_INPUT_H
#define TORNE_RAW_INPUT_H
/* FR30 mode8/9 local CPU memory; never USB register addresses.
 * Only the generic 192-byte input is consumed. B-CAS/image7 is external. */
unsigned int tr_raw_input(unsigned int events);
unsigned int tr_input_clock(void);
#ifdef TR_INPUT_TEST
unsigned char tr_input_read8(unsigned int address);
unsigned short tr_input_read16(unsigned int address);
unsigned int tr_input_read32(unsigned int address);
void tr_input_write8(unsigned int address, unsigned char value);
void tr_input_write16(unsigned int address, unsigned short value);
void tr_input_write32(unsigned int address, unsigned int value);
#endif
#endif
