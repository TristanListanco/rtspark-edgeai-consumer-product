#include "board.h"

void board_led_init(void)
{
    __HAL_RCC_GPIOF_CLK_ENABLE();

    /* Start with both LEDs off */
    GPIO_PinState off = (LED_ON_LEVEL == GPIO_PIN_SET) ? GPIO_PIN_RESET : GPIO_PIN_SET;
    HAL_GPIO_WritePin(LED_R_PORT, LED_R_PIN, off);
    HAL_GPIO_WritePin(LED_G_PORT, LED_G_PIN, off);

    GPIO_InitTypeDef gpio = {
        .Pin = LED_R_PIN | LED_G_PIN,
        .Mode = GPIO_MODE_OUTPUT_PP,
        .Pull = GPIO_NOPULL,
        .Speed = GPIO_SPEED_FREQ_LOW,
    };
    HAL_GPIO_Init(GPIOF, &gpio);
}

void board_led_set(GPIO_TypeDef *port, uint16_t pin, int on)
{
    GPIO_PinState off = (LED_ON_LEVEL == GPIO_PIN_SET) ? GPIO_PIN_RESET : GPIO_PIN_SET;
    HAL_GPIO_WritePin(port, pin, on ? LED_ON_LEVEL : off);
}

void board_led_toggle(GPIO_TypeDef *port, uint16_t pin)
{
    HAL_GPIO_TogglePin(port, pin);
}
