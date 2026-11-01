/* Winbond W25Qxx SPI NOR flash on SPI2 (CS PB12). SPI2 is shared with the RW007
 * Wi-Fi module (CS PF10), so every transaction takes the SPI2 mutex. */
#ifndef SPI_FLASH_H
#define SPI_FLASH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FLASH_SECTOR_SIZE 4096U
#define FLASH_PAGE_SIZE   256U

typedef struct {
    bool ok;
    uint8_t jedec[3];   /* manufacturer, memory type, capacity code */
    uint32_t size;      /* bytes */
    const char *name;
} flash_info_t;

flash_info_t flash_init(void);
const flash_info_t *flash_info(void);
bool flash_read_jedec(uint8_t id[3]);
bool flash_read(uint32_t addr, uint8_t *buf, size_t len);
bool flash_erase_sector(uint32_t addr);
bool flash_program(uint32_t addr, const uint8_t *buf, size_t len); /* within one page */

/* Write/verify test on the last sector. Runs only if that sector is blank (all 0xFF)
 * unless `force`; leaves it erased. Writes a one-line result into `detail`. */
bool flash_selftest(bool force, char *detail, size_t size);

#endif /* SPI_FLASH_H */
