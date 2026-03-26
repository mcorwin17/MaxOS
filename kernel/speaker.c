#include <stdint.h>

#include "speaker.h"
#include "io.h"
#include "thread.h"

#define PIT_CHANNEL2 0x42
#define PIT_COMMAND  0x43
#define SPEAKER_PORT 0x61

#define PIT_BASE_HZ  1193182

void speaker_tone(uint32_t hz) {
    if (hz == 0) { speaker_off(); return; }

    uint32_t divisor = PIT_BASE_HZ / hz;

    /* Channel 2, lobyte/hibyte, mode 3 (square wave). Same command register
     * as the scheduler tick on channel 0 - the channel bits are what keep
     * them apart, and getting those wrong reprograms the system timer. */
    outb(PIT_COMMAND, 0xB6);
    outb(PIT_CHANNEL2, (uint8_t)(divisor & 0xFF));
    outb(PIT_CHANNEL2, (uint8_t)(divisor >> 8));

    /* Bit 0 gates the timer, bit 1 connects it to the speaker. Both. */
    uint8_t v = inb(SPEAKER_PORT);
    outb(SPEAKER_PORT, v | 0x03);
}

void speaker_off(void) {
    outb(SPEAKER_PORT, inb(SPEAKER_PORT) & ~0x03);
}

void speaker_beep(uint32_t hz, uint32_t ms) {
    speaker_tone(hz);
    thread_sleep_ms(ms);
    speaker_off();
}
