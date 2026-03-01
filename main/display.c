#include "display.h"
#include "u8g2.h"
#include "u8g2_esp32_hal.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_random.h"
#include <string.h>

static const char *TAG = "display";

static QueueHandle_t s_display_queue;
static volatile bool s_is_loading = false;
static u8g2_t u8g2;

// Display settings based on Arduino sketch reference
#define PIN_SDA 5
#define PIN_SCL 6
#define SCREEN_WIDTH 72
#define SCREEN_HEIGHT 40
#define X_OFFSET 0
#define Y_OFFSET 0

#define DISPLAY_QUEUE_LENGTH 3
#define DISPLAY_MSG_MAX_LEN 1024

typedef struct {
    char sender[16];
    char text[];
} display_msg_t;

static void display_task(void *arg)
{
    display_msg_t *msg = NULL;
    int loading_dots = 0;
    bool was_loading = false;
    
    int64_t last_activity_time = esp_timer_get_time();
    int64_t last_loading_time = 0;
    bool is_idle = false;
    
    bool is_blinking = false;
    int64_t next_blink_time = 0;
    int64_t next_move_time = 0;
    int eye_offset_x = 0;
    int eye_offset_y = 0;

    // Draw initial empty frame
    u8g2_ClearBuffer(&u8g2);
    u8g2_DrawFrame(&u8g2, X_OFFSET, Y_OFFSET, SCREEN_WIDTH, SCREEN_HEIGHT);
    u8g2_SendBuffer(&u8g2);

    while (1) {
        TickType_t wait_ticks = pdMS_TO_TICKS(50);
        if (xQueueReceive(s_display_queue, &msg, wait_ticks) == pdTRUE) {
            last_activity_time = esp_timer_get_time();
            is_idle = false;
            was_loading = false;
            ESP_LOGI(TAG, "Displayed message: [%s] %s", msg->sender, msg->text);
            bool new_msg_received;
            
            do {
                new_msg_received = false;
                int text_width = u8g2_GetStrWidth(&u8g2, msg->text);
                
                int sender_y = Y_OFFSET + 12;
                int line_y = Y_OFFSET + 16;
                int text_y = (msg->sender[0] != '\0') ? Y_OFFSET + 28 : Y_OFFSET + 24;
                
                int sender_width = u8g2_GetStrWidth(&u8g2, msg->sender);
                int sender_x = X_OFFSET + 4; // default left aligned
                
                if (strcmp(msg->sender, "User") == 0) {
                    sender_x = X_OFFSET + SCREEN_WIDTH - 4 - sender_width;
                }
                
                for (int x = X_OFFSET + SCREEN_WIDTH; x >= X_OFFSET - text_width; x--) {
                    u8g2_ClearBuffer(&u8g2);
                    u8g2_DrawFrame(&u8g2, X_OFFSET, Y_OFFSET, SCREEN_WIDTH, SCREEN_HEIGHT);
                    
                    int clip_y_start = Y_OFFSET + 1;
                    
                    if (msg->sender[0] != '\0') {
                        u8g2_DrawStr(&u8g2, sender_x, sender_y, msg->sender);
                        u8g2_DrawHLine(&u8g2, X_OFFSET, line_y, SCREEN_WIDTH);
                        clip_y_start = line_y + 1;
                    }
                    
                    u8g2_SetClipWindow(&u8g2, X_OFFSET+1, clip_y_start, X_OFFSET+SCREEN_WIDTH-2, Y_OFFSET+SCREEN_HEIGHT-2);
                    u8g2_DrawStr(&u8g2, x, text_y, msg->text);
                    u8g2_SetMaxClipWindow(&u8g2);
                    
                    u8g2_SendBuffer(&u8g2);
                    
                    display_msg_t *new_msg = NULL;
                    if (xQueueReceive(s_display_queue, &new_msg, 0) == pdTRUE) {
                        new_msg_received = true;
                        free(msg);
                        msg = new_msg;
                        ESP_LOGI(TAG, "New message received while scrolling: [%s] %s", msg->sender, msg->text);
                        break;
                    }
                    
                    vTaskDelay(pdMS_TO_TICKS(10));
                }
            } while (new_msg_received);
            
            free(msg);
            msg = NULL;
            last_activity_time = esp_timer_get_time();
        } else {
            int64_t current_time = esp_timer_get_time();
            if (s_is_loading) {
                last_activity_time = current_time;
                is_idle = false;
                was_loading = true;
                if (current_time - last_loading_time > 300000LL) {
                    loading_dots = (loading_dots + 1) % 4;
                    last_loading_time = current_time;
                    
                    u8g2_ClearBuffer(&u8g2);
                    u8g2_DrawFrame(&u8g2, X_OFFSET, Y_OFFSET, SCREEN_WIDTH, SCREEN_HEIGHT);
                    
                    int sender_y = Y_OFFSET + 12;
                    int line_y = Y_OFFSET + 16;
                    
                    char loading_str[4] = "";
                    for (int i = 0; i < loading_dots; i++) {
                        strcat(loading_str, ".");
                    }
                    
                    u8g2_DrawStr(&u8g2, X_OFFSET + 4, sender_y, loading_str);
                    u8g2_DrawHLine(&u8g2, X_OFFSET, line_y, SCREEN_WIDTH);
                    
                    u8g2_SendBuffer(&u8g2);
                }
            } else {
                if (was_loading) {
                    was_loading = false;
                    last_activity_time = current_time;
                } else if (current_time - last_activity_time > 0LL) { // Immediately enter idle animation
                    bool draw_needed = false;
                    if (!is_idle) {
                        is_idle = true;
                        draw_needed = true;
                    }
                    
                    if (current_time > next_blink_time) {
                        is_blinking = !is_blinking;
                        if (is_blinking) {
                            next_blink_time = current_time + 150000LL; // 150ms blink
                        } else {
                            next_blink_time = current_time + (2000000LL + (esp_random() % 3000000LL)); // 2-5s open
                        }
                        draw_needed = true;
                    }
                    
                    if (current_time > next_move_time && !is_blinking) {
                        eye_offset_x = ((int)(esp_random() % 3) - 1) * 3; // -3, 0, 3
                        eye_offset_y = ((int)(esp_random() % 3) - 1) * 2; // -2, 0, 2
                        next_move_time = current_time + (1000000LL + (esp_random() % 2000000LL)); // 1-3s move
                        draw_needed = true;
                    }
                    
                    if (draw_needed) {
                        u8g2_ClearBuffer(&u8g2);
                        u8g2_DrawFrame(&u8g2, X_OFFSET, Y_OFFSET, SCREEN_WIDTH, SCREEN_HEIGHT);
                        
                        int eye_w = 14;
                        int eye_h = is_blinking ? 4 : 16;
                        int base_y = Y_OFFSET + 20 - eye_h / 2;
                        
                        int left_eye_x = X_OFFSET + 24 - eye_w / 2 + eye_offset_x;
                        int right_eye_x = X_OFFSET + 48 - eye_w / 2 + eye_offset_x;
                        int draw_y = base_y + eye_offset_y;
                        
                        if (is_blinking) {
                            u8g2_DrawBox(&u8g2, left_eye_x, draw_y, eye_w, eye_h);
                            u8g2_DrawBox(&u8g2, right_eye_x, draw_y, eye_w, eye_h);
                        } else {
                            u8g2_DrawRBox(&u8g2, left_eye_x, draw_y, eye_w, eye_h, 3);
                            u8g2_DrawRBox(&u8g2, right_eye_x, draw_y, eye_w, eye_h, 3);
                        }
                        
                        u8g2_SendBuffer(&u8g2);
                    }
                }
            }
        }
    }
}

esp_err_t display_init(void)
{
    ESP_LOGI(TAG, "Initializing OLED Display...");

    s_display_queue = xQueueCreate(DISPLAY_QUEUE_LENGTH, sizeof(display_msg_t *));
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

    size_t text_len = strlen(text);
    if (text_len > DISPLAY_MSG_MAX_LEN) {
        text_len = DISPLAY_MSG_MAX_LEN;
    }

    // Dynamically allocate exact memory needed for the message
    display_msg_t *msg = malloc(sizeof(display_msg_t) + text_len + 1);
    if (!msg) {
        ESP_LOGE(TAG, "Failed to allocate memory for display message");
        return;
    }

    memset(msg, 0, sizeof(display_msg_t) + text_len + 1);

    if (sender && sender[0] != '\0') {
        strncpy(msg->sender, sender, sizeof(msg->sender) - 1);
    }
    
    strncpy(msg->text, text, text_len);
    msg->text[text_len] = '\0';
    
    // Replace newlines with spaces so it prints in one line
    for (int i = 0; msg->text[i] != '\0'; i++) {
        if (msg->text[i] == '\n' || msg->text[i] == '\r') {
            msg->text[i] = ' ';
        }
    }

    // Send the POINTER to the queue
    if (xQueueSend(s_display_queue, &msg, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Display queue full, dropping message");
        free(msg);
    }
}

void display_set_loading(bool is_loading)
{
    s_is_loading = is_loading;
}
