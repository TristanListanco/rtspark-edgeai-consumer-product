/*
 * ST7789V3 on FSMC bank 1, sub-bank 3 (NE3 = PG10), 8-bit data bus.
 * FSMC_A18 (PD13) drives the controller's D/CX line: with an 8-bit bus HADDR maps
 * straight onto A[25:0], so address bit 18 selects command (0) or data (1).
 * Init values and timing follow the RT-Thread RT-Spark BSP (board/ports/lcd/drv_lcd.c).
 */
#include "drivers/lcd.h"

#include "FreeRTOS.h"
#include "board.h"
#include "drivers/font8x16.h"
#include "task.h"

#define LCD_CMD  (*(volatile uint8_t *)0x68000000U)
#define LCD_DATA (*(volatile uint8_t *)0x68040000U)

static void wait_ms(uint32_t ms)
{
    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
        vTaskDelay(pdMS_TO_TICKS(ms));
    } else {
        HAL_Delay(ms);
    }
}

static inline void cmd(uint8_t c) { LCD_CMD = c; }
static inline void data(uint8_t d) { LCD_DATA = d; }

static void fsmc_init(void)
{
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();
    __HAL_RCC_FSMC_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {
        .Mode = GPIO_MODE_AF_PP, .Pull = GPIO_NOPULL, .Speed = GPIO_SPEED_FREQ_VERY_HIGH,
        .Alternate = GPIO_AF12_FSMC,
    };
    /* D2 D3 NOE NWE A18 D0 D1 */
    gpio.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15;
    HAL_GPIO_Init(GPIOD, &gpio);
    /* D4..D7 */
    gpio.Pin = GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10;
    HAL_GPIO_Init(GPIOE, &gpio);
    /* NE3 */
    gpio.Pin = GPIO_PIN_10;
    HAL_GPIO_Init(GPIOG, &gpio);

    SRAM_HandleTypeDef sram = {0};
    sram.Instance = FSMC_NORSRAM_DEVICE;
    sram.Extended = FSMC_NORSRAM_EXTENDED_DEVICE;
    sram.Init.NSBank = FSMC_NORSRAM_BANK3;
    sram.Init.DataAddressMux = FSMC_DATA_ADDRESS_MUX_DISABLE;
    sram.Init.MemoryType = FSMC_MEMORY_TYPE_SRAM;
    sram.Init.MemoryDataWidth = FSMC_NORSRAM_MEM_BUS_WIDTH_8;
    sram.Init.BurstAccessMode = FSMC_BURST_ACCESS_MODE_DISABLE;
    sram.Init.WaitSignalPolarity = FSMC_WAIT_SIGNAL_POLARITY_LOW;
    sram.Init.WrapMode = FSMC_WRAP_MODE_DISABLE;
    sram.Init.WaitSignalActive = FSMC_WAIT_TIMING_BEFORE_WS;
    sram.Init.WriteOperation = FSMC_WRITE_OPERATION_ENABLE;
    sram.Init.WaitSignal = FSMC_WAIT_SIGNAL_DISABLE;
    sram.Init.ExtendedMode = FSMC_EXTENDED_MODE_ENABLE;
    sram.Init.AsynchronousWait = FSMC_ASYNCHRONOUS_WAIT_DISABLE;
    sram.Init.WriteBurst = FSMC_WRITE_BURST_DISABLE;
    sram.Init.PageSize = FSMC_PAGE_SIZE_NONE;

    /* Read: ADDSET 15 + DATAST 60 HCLK (~450 ns) for the slow ST7789 read cycle.
     * Write: ADDSET 3 + DATAST 2 HCLK (~30 ns), the BSP's fast write timing. */
    FSMC_NORSRAM_TimingTypeDef rd = {
        .AddressSetupTime = 15, .AddressHoldTime = 0, .DataSetupTime = 60,
        .BusTurnAroundDuration = 0, .CLKDivision = 0, .DataLatency = 0, .AccessMode = FSMC_ACCESS_MODE_A,
    };
    FSMC_NORSRAM_TimingTypeDef wr = {
        .AddressSetupTime = 3, .AddressHoldTime = 0, .DataSetupTime = 2,
        .BusTurnAroundDuration = 0, .CLKDivision = 0, .DataLatency = 0, .AccessMode = FSMC_ACCESS_MODE_A,
    };
    HAL_SRAM_Init(&sram, &rd, &wr);
}

lcd_info_t lcd_init(void)
{
    lcd_info_t info = {0};

    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();
    GPIO_InitTypeDef gpio = {.Mode = GPIO_MODE_OUTPUT_PP, .Pull = GPIO_NOPULL, .Speed = GPIO_SPEED_FREQ_LOW};
    HAL_GPIO_WritePin(LCD_BL_PORT, LCD_BL_PIN, GPIO_PIN_RESET);
    gpio.Pin = LCD_BL_PIN;
    HAL_GPIO_Init(LCD_BL_PORT, &gpio);
    gpio.Pin = LCD_RST_PIN;
    HAL_GPIO_Init(LCD_RST_PORT, &gpio);

    fsmc_init();

    HAL_GPIO_WritePin(LCD_RST_PORT, LCD_RST_PIN, GPIO_PIN_RESET);
    wait_ms(20);
    HAL_GPIO_WritePin(LCD_RST_PORT, LCD_RST_PIN, GPIO_PIN_SET);
    wait_ms(120);

    /* RDDID: dummy byte, then manufacturer ID, version ID, module ID */
    cmd(0x04);
    (void)LCD_DATA;
    info.id[0] = LCD_DATA;
    info.id[1] = LCD_DATA;
    info.id[2] = LCD_DATA;
    /* The BSP identifies ST7789V3 as id[1]:id[2] == 0x81:0xB3 */
    info.ok = (info.id[1] == 0x81 && info.id[2] == 0xB3);

    static const uint8_t seq[] = {
        /* cmd, n, data... */
        0x36, 1, 0x00,                         /* MADCTL: normal orientation */
        0x3A, 1, 0x65,                         /* COLMOD: 16 bit/pixel */
        0xB2, 5, 0x0C, 0x0C, 0x00, 0x33, 0x33, /* porch */
        0xB7, 1, 0x35,                         /* gate control */
        0xBB, 1, 0x37,                         /* VCOM */
        0xC0, 1, 0x2C,                         /* LCM control */
        0xC2, 1, 0x01,                         /* VDV/VRH enable */
        0xC3, 1, 0x12,                         /* VRH */
        0xC4, 1, 0x20,                         /* VDV */
        0xC6, 1, 0x0F,                         /* 60 Hz frame rate */
        0xD0, 2, 0xA4, 0xA1,                   /* power control 1 */
        0xE0, 14, 0xD0, 0x04, 0x0D, 0x11, 0x13, 0x2B, 0x3F, 0x54, 0x4C, 0x18, 0x0D, 0x0B, 0x1F, 0x23,
        0xE1, 14, 0xD0, 0x04, 0x0C, 0x11, 0x13, 0x2C, 0x3F, 0x44, 0x51, 0x2F, 0x1F, 0x1F, 0x20, 0x23,
        0x21, 0,                               /* display inversion on (panel needs it) */
    };
    for (unsigned i = 0; i < sizeof seq;) {
        uint8_t c = seq[i++];
        uint8_t n = seq[i++];
        cmd(c);
        while (n--) {
            data(seq[i++]);
        }
    }
    cmd(0x11); /* sleep out */
    wait_ms(120);
    cmd(0x29); /* display on */

    lcd_fill(COLOR_BLACK);
    lcd_backlight(true);
    return info;
}

void lcd_backlight(bool on)
{
    HAL_GPIO_WritePin(LCD_BL_PORT, LCD_BL_PIN, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static void set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    cmd(0x2A);
    data((uint8_t)(x0 >> 8));
    data((uint8_t)x0);
    data((uint8_t)(x1 >> 8));
    data((uint8_t)x1);
    cmd(0x2B);
    data((uint8_t)(y0 >> 8));
    data((uint8_t)y0);
    data((uint8_t)(y1 >> 8));
    data((uint8_t)y1);
    cmd(0x2C);
}

void lcd_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    if (x >= LCD_W || y >= LCD_H || w == 0 || h == 0) return;
    if (x + w > LCD_W) w = LCD_W - x;
    if (y + h > LCD_H) h = LCD_H - y;
    set_window(x, y, (uint16_t)(x + w - 1), (uint16_t)(y + h - 1));
    uint8_t hi = (uint8_t)(color >> 8), lo = (uint8_t)color;
    for (uint32_t n = (uint32_t)w * h; n; n--) {
        data(hi);
        data(lo);
    }
}

void lcd_fill(uint16_t color)
{
    lcd_fill_rect(0, 0, LCD_W, LCD_H, color);
}

uint16_t lcd_text(uint16_t x, uint16_t y, const char *s, uint16_t fg, uint16_t bg, uint8_t scale)
{
    uint16_t cw = (uint16_t)(FONT_W * scale), ch = (uint16_t)(FONT_H * scale);
    uint8_t fh = (uint8_t)(fg >> 8), fl = (uint8_t)fg, bh = (uint8_t)(bg >> 8), bl = (uint8_t)bg;

    for (; *s && x + cw <= LCD_W; s++, x = (uint16_t)(x + cw)) {
        char c = (*s < FONT_FIRST || *s > FONT_LAST) ? '?' : *s;
        const uint8_t *glyph = font8x16[c - FONT_FIRST];
        if (y + ch > LCD_H) break;
        set_window(x, y, (uint16_t)(x + cw - 1), (uint16_t)(y + ch - 1));
        for (int row = 0; row < FONT_H; row++) {
            for (uint8_t sy = 0; sy < scale; sy++) {
                for (int col = 0; col < FONT_W; col++) {
                    bool on = glyph[row] & (0x80U >> col);
                    for (uint8_t sx = 0; sx < scale; sx++) {
                        data(on ? fh : bh);
                        data(on ? fl : bl);
                    }
                }
            }
        }
    }
    return x;
}
