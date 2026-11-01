#ifndef FAULT_H
#define FAULT_H

/* Stop the system: interrupts off, LED_R blinks fast forever. */
void fault_halt(void) __attribute__((noreturn));

#endif /* FAULT_H */
