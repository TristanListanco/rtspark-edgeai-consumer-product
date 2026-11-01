/*
 * W25Qxx on the shared SPI2 bus (see drivers/spi2_bus.c), chip select PB12.
 */
#include "drivers/spi_flash.h"

#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "board.h"
#include "drivers/spi2_bus.h"
#include "task.h"

#define CMD_WRITE_ENABLE 0x06
#define CMD_READ_STATUS1 0x05
#define CMD_READ_DATA    0x03
#define CMD_PAGE_PROGRAM 0x02
#define CMD_SECTOR_ERASE 0x20
#define CMD_JEDEC_ID     0x9F
#define STATUS_BUSY      0x01

#define ERASE_TIMEOUT_MS 500U  /* datasheet max 400 ms for a 4 KB sector */
#define PROG_TIMEOUT_MS  10U   /* datasheet max 3 ms per page */

static flash_info_t info;

static void lock(void) { spi2_acquire(); }
static void unlock(void) { spi2_release(); }
static void cs_low(void) { HAL_GPIO_WritePin(FLASH_CS_PORT, FLASH_CS_PIN, GPIO_PIN_RESET); }
static void cs_high(void) { HAL_GPIO_WritePin(FLASH_CS_PORT, FLASH_CS_PIN, GPIO_PIN_SET); }

static bool xfer(const uint8_t *tx, size_t txlen, uint8_t *rx, size_t rxlen)
{
    bool ok = true;
    cs_low();
    if (txlen) ok = spi2_tx(tx, (uint16_t)txlen);
    if (ok && rxlen) ok = spi2_rx(rx, (uint16_t)rxlen);
    cs_high();
    return ok;
}

static bool wait_ready(uint32_t timeout_ms)
{
    uint32_t start = HAL_GetTick();
    uint8_t cmd = CMD_READ_STATUS1, status = STATUS_BUSY;
    do {
        if (!xfer(&cmd, 1, &status, 1)) return false;
        if (!(status & STATUS_BUSY)) return true;
        vTaskDelay(1);
    } while (HAL_GetTick() - start < timeout_ms);
    return false;
}

static bool write_enable(void)
{
    uint8_t cmd = CMD_WRITE_ENABLE;
    return xfer(&cmd, 1, NULL, 0);
}

flash_info_t flash_init(void)
{
    spi2_init();

    memset(&info, 0, sizeof info);
    info.name = "unknown";
    if (!flash_read_jedec(info.jedec)) return info;
    /* Winbond (0xEF) W25Qxx: capacity code n -> 2^n bytes (0x17 = 8 MB, 0x18 = 16 MB) */
    if (info.jedec[0] == 0xEF && info.jedec[1] == 0x40 && info.jedec[2] >= 0x14 && info.jedec[2] <= 0x19) {
        static const char *const names[] = {"W25Q80", "W25Q16", "W25Q32", "W25Q64", "W25Q128", "W25Q256"};
        info.size = 1UL << info.jedec[2];
        info.name = names[info.jedec[2] - 0x14];
        info.ok = true;
    } else if (info.jedec[0] != 0x00 && info.jedec[0] != 0xFF) {
        info.size = 1UL << (info.jedec[2] & 0x1F);
        info.name = "non-Winbond SPI NOR";
        info.ok = true;
    }
    return info;
}

const flash_info_t *flash_info(void)
{
    return &info;
}

bool flash_read_jedec(uint8_t id[3])
{
    uint8_t cmd = CMD_JEDEC_ID;
    lock();
    bool ok = xfer(&cmd, 1, id, 3);
    unlock();
    return ok;
}

bool flash_read(uint32_t addr, uint8_t *buf, size_t len)
{
    uint8_t cmd[4] = {CMD_READ_DATA, (uint8_t)(addr >> 16), (uint8_t)(addr >> 8), (uint8_t)addr};
    lock();
    bool ok = wait_ready(PROG_TIMEOUT_MS) && xfer(cmd, sizeof cmd, buf, len);
    unlock();
    return ok;
}

bool flash_erase_sector(uint32_t addr)
{
    uint8_t cmd[4] = {CMD_SECTOR_ERASE, (uint8_t)(addr >> 16), (uint8_t)(addr >> 8), (uint8_t)addr};
    lock();
    bool ok = wait_ready(PROG_TIMEOUT_MS) && write_enable() && xfer(cmd, sizeof cmd, NULL, 0) &&
              wait_ready(ERASE_TIMEOUT_MS);
    unlock();
    return ok;
}

bool flash_program(uint32_t addr, const uint8_t *buf, size_t len)
{
    if (len == 0 || (addr % FLASH_PAGE_SIZE) + len > FLASH_PAGE_SIZE) return false;
    uint8_t cmd[4] = {CMD_PAGE_PROGRAM, (uint8_t)(addr >> 16), (uint8_t)(addr >> 8), (uint8_t)addr};
    lock();
    bool ok = wait_ready(PROG_TIMEOUT_MS) && write_enable();
    if (ok) {
        cs_low();
        ok = spi2_tx(cmd, sizeof cmd) && spi2_tx(buf, (uint16_t)len);
        cs_high();
    }
    ok = ok && wait_ready(PROG_TIMEOUT_MS);
    unlock();
    return ok;
}

bool flash_selftest(bool force, char *detail, size_t size)
{
    static uint8_t buf[FLASH_PAGE_SIZE];
    if (!info.ok) {
        snprintf(detail, size, "no flash detected");
        return false;
    }
    uint32_t sector = info.size - FLASH_SECTOR_SIZE;

    /* Blank check of the whole sector, one page at a time */
    bool blank = true;
    for (uint32_t off = 0; off < FLASH_SECTOR_SIZE && blank; off += FLASH_PAGE_SIZE) {
        if (!flash_read(sector + off, buf, sizeof buf)) {
            snprintf(detail, size, "read failed");
            return false;
        }
        for (unsigned i = 0; i < sizeof buf; i++) blank &= (buf[i] == 0xFF);
    }
    if (!blank && !force) {
        snprintf(detail, size, "last sector 0x%06lx not blank: skipped (use 'flash test force')", sector);
        return true; /* read path works; write test deliberately not run */
    }

    if (!blank && !flash_erase_sector(sector)) {
        snprintf(detail, size, "erase failed");
        return false;
    }
    for (unsigned i = 0; i < sizeof buf; i++) buf[i] = (uint8_t)(i ^ 0xA5);
    uint32_t t1 = HAL_GetTick();
    if (!flash_program(sector, buf, sizeof buf)) {
        snprintf(detail, size, "program failed");
        return false;
    }
    uint32_t t2 = HAL_GetTick();
    memset(buf, 0, sizeof buf);
    if (!flash_read(sector, buf, sizeof buf)) {
        snprintf(detail, size, "read-back failed");
        return false;
    }
    unsigned bad = 0;
    for (unsigned i = 0; i < sizeof buf; i++) bad += (buf[i] != (uint8_t)(i ^ 0xA5));
    uint32_t t3 = HAL_GetTick();
    bool erased = flash_erase_sector(sector); /* leave it blank again */
    uint32_t t4 = HAL_GetTick();

    snprintf(detail, size, "sector 0x%06lx: program %lu ms, verify %u/256 bad, erase %lu ms%s", sector,
             t2 - t1, bad, t4 - t3, erased ? "" : " (re-erase FAILED)");
    return bad == 0 && erased;
}
