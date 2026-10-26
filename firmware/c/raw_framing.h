#ifndef TORNE_RAW_FRAMING_H
#define TORNE_RAW_FRAMING_H
/* Existing mode8/9 USB media framing, before packet classification.
 * This keeps the existing scrambling-marker conversion; it does not prove
 * that the incoming header represents fully descrambled broadcast TS. */
unsigned int tr_raw_framing(volatile unsigned int *packet,
                           volatile unsigned char *media,
                           volatile unsigned char *state);
#endif
