/* UI task: owns the LCD, LED matrix and buzzer; consumes key events. */
#ifndef UI_H
#define UI_H

#include "drivers/lcd.h"

void ui_start(void);
const lcd_info_t *ui_lcd_info(void);
/* Ask the UI task to run the self-test and show it on the LCD (any key returns). */
void ui_show_diag(void);

#endif /* UI_H */
