/*
 * Serial console on USART1, 115200 8N1.
 *
 * TX: once the scheduler runs, writers copy text into a 4 KB stream buffer and
 * return; DMA2 Stream7 (channel 4 = USART1_TX) drains it in chunks. Only the DMA
 * interrupt reads the stream buffer (FreeRTOS stream buffers allow one reader):
 * a writer that finds DMA idle just pends that interrupt. Writers are serialised by
 * the console mutex. If the link stalls for 200 ms, output is dropped and counted
 * rather than blocking a task. Before the scheduler, and on the fault path, output
 * is polled register writes.
 * RX: the USART1 RXNE interrupt pushes each byte into a second stream buffer.
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
#define TX_BUFFER    4096
#define TX_CHUNK     256
#define TX_IRQ_PRIO  6U
#define TX_STALL_MS  200U
#define DMA_S7_FLAGS (DMA_HIFCR_CTCIF7 | DMA_HIFCR_CHTIF7 | DMA_HIFCR_CTEIF7 | DMA_HIFCR_CDMEIF7 | DMA_HIFCR_CFEIF7)

static UART_HandleTypeDef huart1;
static SemaphoreHandle_t tx_mutex;
static char line[LINE_MAX_LEN];
static StreamBufferHandle_t rx_stream;
static StreamBufferHandle_t tx_stream;
static uint8_t tx_chunk[TX_CHUNK]; /* .bss = main SRAM, reachable by DMA */
static volatile bool tx_dma_busy;
static volatile uint32_t tx_dropped;

static bool rtos_running(void)
{
    return tx_mutex != NULL && xTaskGetSchedulerState() == taskSCHEDULER_RUNNING;
}

static void polled_transmit(const char *text, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        while ((CONSOLE_USART->SR & USART_SR_TXE) == 0U) {
        }
        CONSOLE_USART->DR = (uint8_t)text[i];
    }
    while ((CONSOLE_USART->SR & USART_SR_TC) == 0U) {
    }
}

static void tx_kick(void)
{
    NVIC_SetPendingIRQ(DMA2_Stream7_IRQn); /* the ISR starts DMA if it is idle */
}

static void transmit(const char *text, size_t len)
{
    if (tx_stream == NULL || xTaskGetSchedulerState() != taskSCHEDULER_RUNNING) {
        polled_transmit(text, len);
        return;
    }
    while (len > 0) {
        tx_kick();
        size_t n = xStreamBufferSend(tx_stream, text, len, pdMS_TO_TICKS(TX_STALL_MS));
        if (n == 0) {
            tx_dropped += (uint32_t)len;
            break;
        }
        text += n;
        len -= n;
    }
    tx_kick();
}

static void dma_start(size_t n)
{
    DMA2_Stream7->CR &= ~DMA_SxCR_EN;
    while (DMA2_Stream7->CR & DMA_SxCR_EN) {
    }
    DMA2->HIFCR = DMA_S7_FLAGS;
    DMA2_Stream7->M0AR = (uint32_t)tx_chunk;
    DMA2_Stream7->NDTR = (uint32_t)n;
    tx_dma_busy = true;
    DMA2_Stream7->CR |= DMA_SxCR_EN;
}

void DMA2_Stream7_IRQHandler(void)
{
    uint32_t hisr = DMA2->HISR;
    if (hisr & (DMA_HISR_TCIF7 | DMA_HISR_TEIF7 | DMA_HISR_DMEIF7)) {
        DMA2->HIFCR = DMA_S7_FLAGS;
        tx_dma_busy = false;
    }
    if (!tx_dma_busy && tx_stream != NULL) {
        BaseType_t woken = pdFALSE;
        size_t n = xStreamBufferReceiveFromISR(tx_stream, tx_chunk, sizeof tx_chunk, &woken);
        if (n > 0) {
            dma_start(n);
        }
        portYIELD_FROM_ISR(woken);
    }
}

uint32_t console_tx_dropped(void)
{
    return tx_dropped;
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
    tx_stream = xStreamBufferCreate(TX_BUFFER, 1);
    configASSERT(tx_mutex != NULL && tx_stream != NULL);

    /* DMA2 Stream7 channel 4: memory -> USART1->DR, byte-wide, direct mode */
    __HAL_RCC_DMA2_CLK_ENABLE();
    DMA2_Stream7->CR = 0;
    while (DMA2_Stream7->CR & DMA_SxCR_EN) {
    }
    DMA2->HIFCR = DMA_S7_FLAGS;
    DMA2_Stream7->PAR = (uint32_t)&CONSOLE_USART->DR;
    DMA2_Stream7->FCR = 0; /* direct mode */
    DMA2_Stream7->CR = (4U << DMA_SxCR_CHSEL_Pos) | DMA_SxCR_DIR_0 | DMA_SxCR_MINC | DMA_SxCR_PL_0 |
                       DMA_SxCR_TCIE | DMA_SxCR_TEIE | DMA_SxCR_DMEIE;
    CONSOLE_USART->CR3 |= USART_CR3_DMAT;
    HAL_NVIC_SetPriority(DMA2_Stream7_IRQn, TX_IRQ_PRIO, 0);
    HAL_NVIC_EnableIRQ(DMA2_Stream7_IRQn);
}

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
    /* Stop the DMA path and write registers directly: works with IRQs off */
    CONSOLE_USART->CR3 &= ~USART_CR3_DMAT;
    DMA2_Stream7->CR &= ~DMA_SxCR_EN;
    polled_transmit(panic_line, len);
}
