#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"

#define LINK_UART      UART_NUM_1
#define LINK_TX_PIN    17
#define LINK_RX_PIN    18
#define LINK_BAUD      9600
#define RX_BUF_SIZE    256

#define BTN_PIN        GPIO_NUM_0
#define LED_PIN        GPIO_NUM_5
#define ACK_TIMEOUT_MS 200
#define MAX_RETRIES    3

static const char *TAG = "LINK";

static void link_init(void)
{
    uart_config_t cfg = {
        .baud_rate  = LINK_BAUD,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(LINK_UART, RX_BUF_SIZE, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(LINK_UART, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(LINK_UART, LINK_TX_PIN, LINK_RX_PIN,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    gpio_set_pull_mode(LINK_RX_PIN, GPIO_PULLUP_ONLY);
}

static void led_init(void)
{
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << LED_PIN,
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
}

static bool read_line(char *buf, size_t len, int timeout_ms)
{
    size_t n = 0;
    TickType_t start = xTaskGetTickCount();

    while ((xTaskGetTickCount() - start) < pdMS_TO_TICKS(timeout_ms)) {
        uint8_t c;
        if (uart_read_bytes(LINK_UART, &c, 1, pdMS_TO_TICKS(10)) == 1) {
            if (c == '\r') continue;
            if (c == '\n') { buf[n] = '\0'; return true; }
            if (n < len - 1) buf[n++] = (char)c;
        }
    }
    buf[n] = '\0';
    return false;
}

static int get_cmd()
{
    char buf[32];
    if (read_line(buf, sizeof buf, ACK_TIMEOUT_MS)) {
        if(strlen(buf) > 0)
            ESP_LOGI(TAG, "RX <- %s", buf);
        if (strcmp(buf, "ON")==0)
            return 1;
        else if (strcmp(buf, "OFF")==0)
            return 0;
    }
    return 2;
}

static bool send_cmd(const char *cmd)
{
    char tx[16], expected[16], rx[32];
    int  tx_len = snprintf(tx, sizeof tx, "%s\n", cmd);
    snprintf(expected, sizeof expected, "ACK %s", cmd);

    for (int attempt = 1; attempt <= MAX_RETRIES; attempt++) {
        uart_flush_input(LINK_UART);
        uart_write_bytes(LINK_UART, tx, tx_len);
        ESP_LOGI(TAG, "TX -> %s (спроба %d)", cmd, attempt);

        if (read_line(rx, sizeof rx, ACK_TIMEOUT_MS) && strcmp(rx, expected) == 0) {
            ESP_LOGI(TAG, "RX <- %s", rx);
            return true;
        }
        ESP_LOGW(TAG, "NO ACK (отримано: '%s')", rx);
    }
    ESP_LOGE(TAG, "STM32 не відповідає після %d спроб", MAX_RETRIES);
    return false;
}

void app_main(void)
{
    link_init();
    led_init();
    gpio_config_t btn = {
        .pin_bit_mask = 1ULL << BTN_PIN,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&btn));

    bool led_on = false;
    ESP_LOGI(TAG, "Старт. UART1 %d 8N1, TX=%d RX=%d, кнопка GPIO%d",
             LINK_BAUD, LINK_TX_PIN, LINK_RX_PIN, BTN_PIN);
    gpio_set_level(LED_PIN, 0);
    while (1) {
        if (gpio_get_level(BTN_PIN) == 0) {            
            vTaskDelay(pdMS_TO_TICKS(20));            
            if (gpio_get_level(BTN_PIN) == 0) {
                led_on = !led_on;
                if (!send_cmd(led_on ? "ON" : "OFF")) {
                    led_on = !led_on;                  
                }
                while (gpio_get_level(BTN_PIN) == 0) { 
                    vTaskDelay(pdMS_TO_TICKS(10));
                }
            }
        }
        int rez = get_cmd();
        if (rez < 2) {
            gpio_set_level(LED_PIN, rez);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}