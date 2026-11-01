/* microSD over SDIO 4-bit with DMA (DMA2 Stream3 RX / Stream6 TX). No card-detect pin
 * on RT-Spark: presence is decided by card initialisation. */
#ifndef SDCARD_H
#define SDCARD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    bool present;
    int error;              /* HAL error code when not present */
    uint32_t blocks;        /* logical 512-byte blocks */
    uint8_t type;           /* 0 = SDSC, 1 = SDHC/SDXC */
    uint8_t manufacturer;
    char product[6];
    bool mbr_signature;     /* block 0 ends in 0x55AA */
    uint8_t partition_type; /* first MBR partition type byte (0x0B/0x0C = FAT32) */
} sd_info_t;

sd_info_t sd_init(void);
const sd_info_t *sd_info(void);
/* buf must be 4-byte aligned and in main SRAM (DMA). Returns false on error/timeout. */
bool sd_read_blocks(uint8_t *buf, uint32_t block, uint32_t count);
bool sd_write_blocks(const uint8_t *buf, uint32_t block, uint32_t count);

#endif /* SDCARD_H */
