/*
 * Serial console on USART1. Polled (blocking) transmit: at 115200 baud a
 * 100-character line takes ~8.7 ms. Fine for bring-up; move to DMA/stream buffer
 * if logging ever delays a time-critical task.
 */
#include "console.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "board.h"
#include "semphr.h"
#include "stream_buffer.h"
#include "task.h"

#define LINE_MAX_LEN 192
#define RX_BUFFER    128
#define RX_IRQ_PRIO  6U /* >= configMAX_SYSCALL_INTERRUPT_PRIORITY: ISR uses FromISR API */

static UART_HandleTypeDef huart1;
static SemaphoreHandle_t tx_mutex;
static char line[LINE_MAX_LEN];
static StreamBufferHandle_t rx_stream;

static bool rtos_running(void)
{
    return tx_mutex != NULL && xTaskGetSchedulerState() == taskSCHEDULER_RUNNING;
}

static void transmit(const char *text, size_t len)
{
    HAL_UART_Transmit(&huart1, (const uint8_t *)text, (uint16_t)len, 1000U);
}

/* Format "[  sss.mmm] message\r\n" into line[]; returns its length. */
static size_t format_line(const char *fmt, va_list ap)
{
    uint32_t now = HAL_GetTick();
    int n = snprintf(line, sizeof line, "[%6lu.%03lu] ", now / 1000U, now % 1000U);
    int m = vsnprintf(line + n, sizeof line - (size_t)n - 2U, fmt, ap);
    size_t len = (size_t)n + (m < 0 ? 0U : (size_t)m);
    if (len > sizeof line - 3U) {
        len = sizeof line - 3U; /* truncated */
    }
    line[len++] = '\r';
    line[len++] = '\n';
    line[len] = '\0';
    return len;
}

void console_init(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_USART1_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {
        .Pin = GPIO_PIN_9 | GPIO_PIN_10,
        .Mode = GPIO_MODE_AF_PP,
        .Pull = GPIO_PULLUP,
        .Speed = GPIO_SPEED_FREQ_VERY_HIGH,
        .Alternate = GPIO_AF7_USART1,
    };
    HAL_GPIO_Init(GPIOA, &gpio);

    huart1.Instance = CONSOLE_USART;
    huart1.Init.BaudRate = CONSOLE_BAUD;
    huart1.Init.WordLength = UART_WORDLENGTH_8B;
    huart1.Init.StopBits = UART_STOPBITS_1;
    huart1.Init.Parity = UART_PARITY_NONE;
    huart1.Init.Mode = UART_MODE_TX_RX;
    huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart1.Init.OverSampling = UART_OVERSAMPLING_16;
    HAL_UART_Init(&huart1);
}

void console_rtos_init(void)
{
    tx_mutex = xSemaphoreCreateMutex();
    configASSERT(tx_mutex != NULL);
}

/* RX: the USART1 RXNE interrupt pushes each byte into a stream buffer. TX stays
 * polled through the HAL, which only touches the TX flags. */
void console_rx_start(void)
{
    rx_stream = xStreamBufferCreate(RX_BUFFER, 1);
    configASSERT(rx_stream != NULL);
    __HAL_UART_ENABLE_IT(&huart1, UART_IT_RXNE);
    HAL_NVIC_SetPriority(USART1_IRQn, RX_IRQ_PRIO, 0);
    HAL_NVIC_EnableIRQ(USART1_IRQn);
}

int console_getc(uint32_t timeout_ms)
{
    uint8_t c;
    if (rx_stream == NULL || xStreamBufferReceive(rx_stream, &c, 1, pdMS_TO_TICKS(timeout_ms)) != 1) {
        return -1;
    }
    return c;
}

void USART1_IRQHandler(void)
{
    uint32_t sr = CONSOLE_USART->SR;
    if (sr & (USART_SR_RXNE | USART_SR_ORE)) {
        uint8_t c = (uint8_t)CONSOLE_USART->DR; /* SR then DR read also clears ORE */
        BaseType_t woken = pdFALSE;
        (void)xStreamBufferSendFromISR(rx_stream, &c, 1, &woken);
        portYIELD_FROM_ISR(woken);
    }
}

void console_lock(void)
{
    if (rtos_running()) {
        xSemaphoreTake(tx_mutex, portMAX_DELAY);
    }
}

void console_unlock(void)
{
    if (rtos_running()) {
        xSemaphoreGive(tx_mutex);
    }
}

void console_write(const char *text)
{
    transmit(text, strlen(text));
}

void console_write_n(const char *buf, unsigned len)
{
    transmit(buf, len);
}

void console_printf(const char *fmt, ...)
{
    console_lock();
    va_list ap;
    va_start(ap, fmt);
    size_t len = format_line(fmt, ap);
    va_end(ap);
    transmit(line, len);
    console_unlock();
}

void console_panic(const char *fmt, ...)
{
    static char panic_line[LINE_MAX_LEN];
    va_list ap;
    va_start(ap, fmt);
    int m = vsnprintf(panic_line, sizeof panic_line - 2U, fmt, ap);
    va_end(ap);
    size_t len = (m < 0) ? 0U : (size_t)m;
    if (len > sizeof panic_line - 3U) {
        len = sizeof panic_line - 3U;
    }
    panic_line[len++] = '\r';
    panic_line[len++] = '\n';
    /* Direct register writes: works even if the HAL handle is busy or IRQs are off */
    for (size_t i = 0; i < len; i++) {
        while ((CONSOLE_USART->SR & USART_SR_TXE) == 0U) {
        }
        CONSOLE_USART->DR = (uint8_t)panic_line[i];
    }
    while ((CONSOLE_USART->SR & USART_SR_TC) == 0U) {
    }
}
