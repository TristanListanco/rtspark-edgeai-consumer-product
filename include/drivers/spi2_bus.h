/* SPI2 (PB13 SCK, PC2 MISO, PC3 MOSI), mode 0, 10.5 MHz, shared by the W25Qxx flash
 * (CS PB12) and the RW007 Wi-Fi module (CS PF10). Hold the bus with spi2_acquire()
 * for a whole chip-select transaction. */
#ifndef SPI2_BUS_H
#define SPI2_BUS_H

#include <stdbool.h>
#include <stdint.h>

void spi2_init(void);
void spi2_acquire(void);
void spi2_release(void);
bool spi2_tx(const uint8_t *tx, uint16_t len);
bool spi2_rx(uint8_t *rx, uint16_t len);
bool spi2_txrx(const uint8_t *tx, uint8_t *rx, uint16_t len);
/* Drive SCK as a plain GPIO low (true) or hand it back to SPI2 (false).
 * Used while resetting RW007, whose BOOT0 strap shares the SCK line. */
void spi2_sck_park_low(bool park);

#endif /* SPI2_BUS_H */
