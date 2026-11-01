/* RW007 Wi-Fi module (SPI2, CS PF10, INT/BUSY PG11, RST PG15): presence probe only.
 * The full RT-Thread RW007 network protocol is not ported; the product never needs
 * the network to decide (handout: inference and feedback must work offline). */
#ifndef RW007_H
#define RW007_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool ready_signal;   /* INT/BUSY went high after reset (module booted) */
    uint32_t boot_ms;    /* reset release -> ready */
    bool magic_ok;       /* stage-1 SPI exchange returned the slave magic words */
    uint32_t magic1;
    uint32_t magic2;
    uint8_t phase;       /* slave phase field, 3 = slave_cmd_phase */
    uint8_t attempts;    /* header exchanges tried */
    uint32_t reply_ms;   /* boot -> first valid reply */
} rw007_probe_t;

/* Resets the module, waits for it to boot, does one protocol header exchange,
 * then holds it in reset again (saves power and board self-heating). */
rw007_probe_t rw007_probe(void);
const rw007_probe_t *rw007_last_probe(void);

#endif /* RW007_H */
