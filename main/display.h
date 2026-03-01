#ifndef ZCLAW_DISPLAY_H
#define ZCLAW_DISPLAY_H

#include <stdbool.h>
#include "esp_err.h"

// Initialize OLED display (I2C pin 5 and 6)
esp_err_t display_init(void);

// Queue a message to be shown on screen
void display_show_message(const char *sender, const char *text);

// Set loading animation state
void display_set_loading(bool is_loading);

#endif // ZCLAW_DISPLAY_H
