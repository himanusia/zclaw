#ifndef ZCLAW_DISPLAY_H
#define ZCLAW_DISPLAY_H

#include "esp_err.h"

// Initialize OLED display (I2C pin 5 and 6)
esp_err_t display_init(void);

// Queue a message to be shown on screen
void display_show_message(const char *text);

#endif // ZCLAW_DISPLAY_H
