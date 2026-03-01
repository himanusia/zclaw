#include "display.h"
#include "u8g2.h"
#include "u8g2_esp32_hal.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "display";

static QueueHandle_t s_display_queue;
static u8g2_t u8g2;

// Display settings based on Arduino sketch reference
#define PIN_SDA 5
#define PIN_SCL 6
#define SCREEN_WIDTH 72
#define SCREEN_HEIGHT 40
#define X_OFFSET 0  // 72x40 driver might not need the 28 offset anymore
#define Y_OFFSET 0  // 72x40 driver might not need the 24 offset anymore

#define DISPLAY_QUEUE_LENGTH 2
#define DISPLAY_MSG_MAX_LEN 128

typedef struct {
    char text[DISPLAY_MSG_MAX_LEN];
} display_msg_t;

static void display_task(void *arg)
{
    display_msg_t msg;

    // Draw initial empty frame
    u8g2_ClearBuffer(&u8g2);
    u8g2_DrawFrame(&u8g2, X_OFFSET, Y_OFFSET, SCREEN_WIDTH, SCREEN_HEIGHT);
    u8g2_SendBuffer(&u8g2);

    while (1) {
        if (xQueueReceive(s_display_queue, &msg, portMAX_DELAY) == pdTRUE) {
            ESP_LOGI(TAG, "Displayed message: %s", msg.text);
            bool new_msg_received;
            
            do {
                new_msg_received = false;
                int text_width = u8g2_GetStrWidth(&u8g2, msg.text);
                int text_y = Y_OFFSET + 24;
                
                for (int x = X_OFFSET + SCREEN_WIDTH; x >= X_OFFSET - text_width; x--) {
                    u8g2_ClearBuffer(&u8g2);
                    u8g2_DrawFrame(&u8g2, X_OFFSET, Y_OFFSET, SCREEN_WIDTH, SCREEN_HEIGHT);
                    
                    u8g2_SetClipWindow(&u8g2, X_OFFSET+1, Y_OFFSET+1, X_OFFSET+SCREEN_WIDTH-2, Y_OFFSET+SCREEN_HEIGHT-2);
                    u8g2_DrawStr(&u8g2, x, text_y, msg.text);
                    u8g2_SetMaxClipWindow(&u8g2);
                    
                    u8g2_SendBuffer(&u8g2);
                    
                    if (xQueueReceive(s_display_queue, &msg, 0) == pdTRUE) {
                        new_msg_received = true;
                        ESP_LOGI(TAG, "New message received while scrolling: %s", msg.text);
                        break;
                    }
                    
                    vTaskDelay(pdMS_TO_TICKS(10));
                }
            } while (new_msg_received);
            
            // Draw empty frame after scrolling finishes
            u8g2_ClearBuffer(&u8g2);
            u8g2_DrawFrame(&u8g2, X_OFFSET, Y_OFFSET, SCREEN_WIDTH, SCREEN_HEIGHT);
            u8g2_SendBuffer(&u8g2);
        }
    }
}

esp_err_t display_init(void)
{
    ESP_LOGI(TAG, "Initializing OLED Display...");

    s_display_queue = xQueueCreate(DISPLAY_QUEUE_LENGTH, sizeof(display_msg_t));
    if (!s_display_queue) {
        ESP_LOGE(TAG, "Failed to create display queue");
        return ESP_ERR_NO_MEM;
    }

    u8g2_esp32_hal_t u8g2_esp32_hal = U8G2_ESP32_HAL_DEFAULT;
    u8g2_esp32_hal.bus.i2c.sda = PIN_SDA;
    u8g2_esp32_hal.bus.i2c.scl = PIN_SCL;
    u8g2_esp32_hal_init(u8g2_esp32_hal);

    u8g2_Setup_ssd1306_i2c_72x40_er_f(
        &u8g2,
        U8G2_R0,
        u8g2_esp32_i2c_byte_cb,
        u8g2_esp32_gpio_and_delay_cb); // init u8g2 structure

    u8x8_SetI2CAddress(&u8g2.u8x8, 0x78); // set I2C address (0x3C << 1)

    u8g2_InitDisplay(&u8g2); // send init sequence to the display, display is in sleep mode after this
    u8g2_SetPowerSave(&u8g2, 0); // wake up display
    u8g2_SetFont(&u8g2, u8g2_font_amstrad_cpc_extended_8r); // same font as Arduino sketch

    if (xTaskCreate(display_task, "display_task", 2048, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create display task");
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

void display_show_message(const char *sender, const char *text)
{
    if (!s_display_queue || !text) return;

    display_msg_t msg;
    if (sender && sender[0] != '\0') {
        snprintf(msg.text, DISPLAY_MSG_MAX_LEN, "%s: %s", sender, text);
    } else {
        strncpy(msg.text, text, DISPLAY_MSG_MAX_LEN - 1);
        msg.text[DISPLAY_MSG_MAX_LEN - 1] = '\0';
    }
    
    // Replace newlines with spaces so it prints in one line (since we use DrawStr)
    for (int i = 0; msg.text[i] != '\0'; i++) {
        if (msg.text[i] == '\n' || msg.text[i] == '\r') {
            msg.text[i] = ' ';
        }
    }

    xQueueSend(s_display_queue, &msg, 0);
}
