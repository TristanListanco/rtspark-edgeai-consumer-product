/* 19 x WS2812 RGB LEDs on PA7 (TIM3_CH2 PWM + DMA1 Stream5). Enable pin PF2 (low = on). */
#ifndef WS2812_H
#define WS2812_H

#include <stdbool.h>
#include <stdint.h>

#define WS2812_COUNT 19U

void ws2812_init(void);
void ws2812_set(unsigned index, uint8_t r, uint8_t g, uint8_t b);
void ws2812_fill(uint8_t r, uint8_t g, uint8_t b);
/* Start a DMA refresh; returns false if the previous one is still running. */
bool ws2812_show(void);
/* Frames whose DMA transfer completed (diagnostics). */
uint32_t ws2812_frames_done(void);

#endif /* WS2812_H */
