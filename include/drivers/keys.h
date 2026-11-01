/* Four onboard keys on EXTI (both edges) with a debounce task.
 * Active level is learned at init from each key's idle level (logged as evidence). */
#ifndef KEYS_H
#define KEYS_H

#include <stdint.h>

#include "FreeRTOS.h"
#include "queue.h"

typedef enum { KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT, KEY_COUNT } key_id_t;
typedef enum { KEY_EVT_PRESS, KEY_EVT_LONG, KEY_EVT_RELEASE } key_evt_type_t;

typedef struct {
    key_id_t key;
    key_evt_type_t type;
    uint32_t tick_ms;  /* HAL tick when the event was decided */
    uint32_t held_ms;  /* for LONG / RELEASE */
} key_event_t;

#define KEY_LONG_PRESS_MS 1500U
#define KEY_DEBOUNCE_MS   20U

/* Starts the key task; events are posted to `events` (never blocks). */
void keys_init(QueueHandle_t events);
uint8_t keys_idle_mask(void); /* bit k set = key k idles high */
uint32_t keys_event_count(void);
const char *key_name(key_id_t k);
const char *key_evt_name(key_evt_type_t t);

#endif /* KEYS_H */
