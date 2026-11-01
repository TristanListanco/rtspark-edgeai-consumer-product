/* Onboard buzzer on PB0, driven with a TIM8_CH2N square wave so it sounds whether
 * the part is an active or a passive buzzer. Patterns play from a small task. */
#ifndef BUZZER_H
#define BUZZER_H

#include <stdbool.h>
#include <stdint.h>

#define BUZZER_TONE_HZ 2700U

void buzzer_init(void);
/* Queue a pattern of alternating on/off durations in ms (starts with "on").
 * Non-blocking; returns 0 if the queue is full. Max 8 steps. */
int buzzer_play(const uint16_t *steps, unsigned count);
int buzzer_beep(uint16_t ms);
/* TIM8 running with its complementary output enabled (diagnostics). */
bool buzzer_ok(void);

#endif /* BUZZER_H */
