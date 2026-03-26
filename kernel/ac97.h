/* AC97 audio (Intel 82801AA as qemu presents it).
 *
 * Bus-master DMA: a buffer descriptor list points at PCM buffers in RAM and
 * the card walks it on its own. That's the interesting part, and the reason
 * this is a better subject than the PC speaker - the speaker is one timer
 * channel and a square wave, whereas this is a device reading memory the
 * kernel handed it while the kernel gets on with something else.
 *
 * 16-bit signed stereo at 48 kHz, which is the AC97 baseline and needs no
 * rate conversion. */

#ifndef AC97_H
#define AC97_H

#include <stdint.h>

#define AC97_RATE     48000
#define AC97_CHANNELS 2

int ac97_initialize(void);
int ac97_present(void);

/* Queue interleaved stereo frames. Blocks until the card has room, so a
 * caller can just push a stream. Returns frames accepted. */
uint32_t ac97_play(const int16_t* frames, uint32_t frame_count);

/* Wait for everything queued to finish, then stop the engine. */
void ac97_drain(void);

/* Play a tone for a duration. Non-zero if there's no card. */
int ac97_tone(uint32_t hz, uint32_t ms, uint32_t volume_pct);

/* A short melody, so the test has something with structure to check. */
int ac97_demo(void);

#endif
