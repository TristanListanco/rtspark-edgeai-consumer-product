/* Serial console on USART1 (115200 8N1).
 * Every console_printf() line is prefixed with the HAL uptime and is atomic:
 * a mutex serialises tasks once the scheduler runs. */
#ifndef CONSOLE_H
#define CONSOLE_H

void console_init(void);
void console_rtos_init(void);

void console_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* Multi-line blocks: hold the console across several console_write() calls. */
void console_lock(void);
void console_unlock(void);
void console_write(const char *text);
void console_write_n(const char *buf, unsigned len);

/* Fault path: no RTOS, no mutex, polled output. Safe with interrupts disabled. */
void console_panic(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#endif /* CONSOLE_H */
