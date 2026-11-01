/* Non-destructive checks for interfaces whose wiring is not documented in the BSP
 * (CAN, RS485): which candidate pins are actively driven by an external chip, and
 * a one-shot CAN frame whose error code tells "transceiver echoes" from "nothing". */
#ifndef IO_PROBE_H
#define IO_PROBE_H

#include <stdbool.h>
#include <stdint.h>

#include "stm32f4xx_hal.h"

typedef enum { LINE_FLOATING, LINE_DRIVEN_HIGH, LINE_DRIVEN_LOW } line_state_t;

typedef struct {
    line_state_t pb8, pb9;  /* CAN1 RX / TX candidates (also the "EEPROM" i2c1 pins) */
    bool tx_attempted;
    bool tx_ok;             /* another node acknowledged */
    uint8_t lec;            /* CAN_ESR last error code */
    const char *verdict;
} can_probe_t;

typedef struct {
    line_state_t pc7, pg9;  /* USART6 RX candidates, with PG8 (DE/RE) = receive */
    const char *verdict;
} rs485_probe_t;

line_state_t io_line_state(GPIO_TypeDef *port, uint16_t pin);
const char *io_line_str(line_state_t s);
can_probe_t can_probe(void);
rs485_probe_t rs485_probe(void);
const can_probe_t *can_last_probe(void);
const rs485_probe_t *rs485_last_probe(void);

#endif /* IO_PROBE_H */
