/* 8253/8254 PIT, channel 0, wired to IRQ0. */

#ifndef PIT_H
#define PIT_H

#include <stdint.h>

#define PIT_FREQUENCY_HZ  100

void     pit_initialize(uint32_t frequency_hz);
uint32_t pit_ticks(void);
uint32_t pit_uptime_ms(void);

/* Blocks until enough ticks have gone by. Needs interrupts enabled - with
 * cli set this never returns. */
void sleep_ms(uint32_t ms);

/* Start driving the scheduler from the tick. Off until threads exist. */
void pit_enable_preemption(void);

#endif
