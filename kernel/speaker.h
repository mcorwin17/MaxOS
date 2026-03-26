/* PC speaker: PIT channel 2 gated onto a physical cone.
 *
 * The whole device is one timer channel and two bits of a port. Square
 * waves at one volume, no mixing, no DMA - which is exactly why it's worth
 * keeping next to the AC97 driver. One of them is a frequency divisor; the
 * other is a device reading buffers out of RAM on its own. */

#ifndef SPEAKER_H
#define SPEAKER_H

#include <stdint.h>

void speaker_tone(uint32_t hz);
void speaker_off(void);
void speaker_beep(uint32_t hz, uint32_t ms);

#endif
