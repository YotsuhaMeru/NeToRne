#ifndef TORNE_RAW_OUTPUT_H
#define TORNE_RAW_OUTPUT_H
/* FR30 mode8/9 local addresses. They are not USB register numbers.
 * The caller is the legacy single-threaded event loop. */
unsigned int tr_raw_output(unsigned int events);
#ifdef TR_OUTPUT_TEST
unsigned char tr_output_read8(unsigned int address);
unsigned short tr_output_read16(unsigned int address);
unsigned int tr_output_read32(unsigned int address);
void tr_output_write8(unsigned int address, unsigned char value);
void tr_output_write16(unsigned int address, unsigned short value);
void tr_output_write32(unsigned int address, unsigned int value);
#endif
#endif
