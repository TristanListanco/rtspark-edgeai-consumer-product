/*
 * Labeled data capture for the training dataset (M4). Streams raw board samples
 * over the console in a line protocol that ml/capture.py records:
 *
 *   H,1,<session>,<label>,key=value,...   session header (scales, firmware, context)
 *   T,<trial>,start,<t_ms>,<seconds>      trial begins (after the GO beep)
 *   D,<seq>,<t_ms>,ax,ay,az,gx,gy,gz      ICM20608 raw sample (100 Hz)
 *   L,<t_ms>,als,ps,ir,flags              AP3216C raw sample (10 Hz); flags b0 obj, b1 IR overflow
 *   E,<t_ms>,imu|light                    read error at that time
 *   T,<trial>,end,<t_ms>,<D lines>,<dropped>
 *   C,stop,<session>
 *
 * Samples are only produced inside a trial. Console log lines ("[ t] ...") may be
 * interleaved; the host ignores them.
 */
#ifndef CAPTURE_H
#define CAPTURE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "drivers/ap3216c.h"
#include "drivers/icm20608.h"

void capture_start(void);

/* Called by the sensor task; never block. */
bool capture_active(void);
void capture_push_imu(uint32_t seq, uint32_t t_ms, const icm20608_raw_t *raw);
void capture_push_light(uint32_t t_ms, const ap3216c_reading_t *r);
void capture_push_error(uint32_t t_ms, bool imu);

/* Called by the CLI. capture_trial() blocks for the countdown + trial. */
bool capture_session_start(const char *label, const char *session);
bool capture_trial(unsigned trial, unsigned seconds);
void capture_session_stop(void);
/* Short status for the LCD, e.g. "REC OPENED t3"; empty when idle. */
void capture_status(char *buf, size_t size);

#endif /* CAPTURE_H */
