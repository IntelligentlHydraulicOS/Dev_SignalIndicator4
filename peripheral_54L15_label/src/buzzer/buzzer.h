#ifndef BUZZER_H
#define BUZZER_H

#include <stdint.h>

void buzzer_init(void);

/**
 * Start a nonblocking beep pattern.
 *
 * The buzzer sounds for on_ms, stays off for interval_ms between beeps,
 * and repeats count times. The request replaces any pattern already running.
 * Call from thread context, not from an ISR.
 *
 * @return 0 on success, or a negative errno value on failure.
 */
int buzzer_beep(uint32_t on_ms, uint32_t interval_ms, uint16_t count);

/** Start one beep for duration_ms; retained for existing call sites. */
void buzzer_on(uint32_t duration_ms);

#endif /* BUZZER_H */