/*
 * Keys: any edge -> EXTI ISR -> task notification -> debounce task.
 * The task waits KEY_DEBOUNCE_MS after the last edge, then compares the settled
 * levels with the stable state and emits PRESS / RELEASE, plus LONG once while held.
 * EXTI priority 6 (>= configMAX_SYSCALL_INTERRUPT_PRIORITY) so the ISR may notify.
 */
#include "drivers/keys.h"

#include <stdbool.h>

#include "board.h"
#include "task.h"

#define KEYS_IRQ_PRIO 6U

static const struct {
    GPIO_TypeDef *port;
    uint16_t pin;
    IRQn_Type irq;
} key_hw[KEY_COUNT] = {
    [KEY_UP] = {KEY_UP_PORT, KEY_UP_PIN, EXTI9_5_IRQn},     /* PC5 */
    [KEY_DOWN] = {KEY_DOWN_PORT, KEY_DOWN_PIN, EXTI1_IRQn}, /* PC1 */
    [KEY_LEFT] = {KEY_LEFT_PORT, KEY_LEFT_PIN, EXTI0_IRQn}, /* PC0 */
    [KEY_RIGHT] = {KEY_RIGHT_PORT, KEY_RIGHT_PIN, EXTI4_IRQn}, /* PC4 */
};

static TaskHandle_t key_task_handle;
static QueueHandle_t event_queue;
static uint8_t idle_mask;
static uint32_t event_count;

static bool level(key_id_t k)
{
    return HAL_GPIO_ReadPin(key_hw[k].port, key_hw[k].pin) == GPIO_PIN_SET;
}

static bool pressed(key_id_t k)
{
    return level(k) != ((idle_mask >> k) & 1U);
}

static void post(key_id_t k, key_evt_type_t t, uint32_t held)
{
    key_event_t e = {.key = k, .type = t, .tick_ms = HAL_GetTick(), .held_ms = held};
    (void)xQueueSend(event_queue, &e, 0);
    event_count++;
}

static void key_task(void *arg)
{
    (void)arg;
    bool down[KEY_COUNT] = {false};
    bool long_sent[KEY_COUNT] = {false};
    uint32_t down_since[KEY_COUNT] = {0};

    for (;;) {
        bool any_down = false;
        for (int k = 0; k < KEY_COUNT; k++) any_down |= down[k];

        /* Sleep until an edge; while a key is held, also wake every 50 ms for long-press */
        uint32_t edges = 0;
        xTaskNotifyWait(0, UINT32_MAX, &edges, any_down ? pdMS_TO_TICKS(50) : portMAX_DELAY);
        if (edges) {
            /* Debounce: wait until no new edge arrives for KEY_DEBOUNCE_MS */
            while (xTaskNotifyWait(0, UINT32_MAX, &edges, pdMS_TO_TICKS(KEY_DEBOUNCE_MS)) == pdTRUE) {
            }
        }

        uint32_t now = HAL_GetTick();
        for (key_id_t k = 0; k < KEY_COUNT; k++) {
            bool p = pressed(k);
            if (p && !down[k]) {
                down[k] = true;
                long_sent[k] = false;
                down_since[k] = now;
                post(k, KEY_EVT_PRESS, 0);
            } else if (!p && down[k]) {
                down[k] = false;
                post(k, KEY_EVT_RELEASE, now - down_since[k]);
            } else if (p && !long_sent[k] && now - down_since[k] >= KEY_LONG_PRESS_MS) {
                long_sent[k] = true;
                post(k, KEY_EVT_LONG, now - down_since[k]);
            }
        }
    }
}

void keys_init(QueueHandle_t events)
{
    event_queue = events;
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_SYSCFG_CLK_ENABLE();

    /* Pull-up works for either wiring: a switch to GND idles high; a switch to VCC
     * with an external pull-down (stronger than the ~40 k internal one) idles low. */
    GPIO_InitTypeDef gpio = {.Mode = GPIO_MODE_IT_RISING_FALLING, .Pull = GPIO_PULLUP,
                             .Speed = GPIO_SPEED_FREQ_LOW};
    for (int k = 0; k < KEY_COUNT; k++) {
        gpio.Pin = key_hw[k].pin;
        HAL_GPIO_Init(key_hw[k].port, &gpio);
    }

    HAL_Delay(2); /* let the pull-ups settle (scheduler is running: HAL tick is live) */
    idle_mask = 0;
    for (key_id_t k = 0; k < KEY_COUNT; k++) {
        if (level(k)) idle_mask |= (uint8_t)(1U << k);
    }

    BaseType_t ok = xTaskCreate(key_task, "keys", 256, NULL, 3, &key_task_handle);
    configASSERT(ok == pdPASS);

    for (int k = 0; k < KEY_COUNT; k++) {
        HAL_NVIC_SetPriority(key_hw[k].irq, KEYS_IRQ_PRIO, 0);
        HAL_NVIC_EnableIRQ(key_hw[k].irq);
    }
}

uint32_t keys_event_count(void)
{
    return event_count;
}

uint8_t keys_idle_mask(void)
{
    return idle_mask;
}

/* EXTI lines 0, 1, 4 and 5 belong to the keys */
void HAL_GPIO_EXTI_Callback(uint16_t pin)
{
    if (key_task_handle == NULL) return;
    BaseType_t woken = pdFALSE;
    xTaskNotifyFromISR(key_task_handle, pin, eSetBits, &woken);
    portYIELD_FROM_ISR(woken);
}

void EXTI0_IRQHandler(void) { HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_0); }
void EXTI1_IRQHandler(void) { HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_1); }
void EXTI4_IRQHandler(void) { HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_4); }
void EXTI9_5_IRQHandler(void) { HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_5); }

const char *key_name(key_id_t k)
{
    static const char *const names[KEY_COUNT] = {"UP", "DOWN", "LEFT", "RIGHT"};
    return (k < KEY_COUNT) ? names[k] : "?";
}

const char *key_evt_name(key_evt_type_t t)
{
    switch (t) {
    case KEY_EVT_PRESS: return "press";
    case KEY_EVT_LONG:  return "long-press";
    default:            return "release";
    }
}
