#include "serial_handler.h"
#include "shared_state.h"
#include "hw_data.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "serial";

// UART0 ist auf allen unterstuetzten Chips (ESP32/S3/C3) diejenige Leitung,
// die ueber den Onboard-USB-Seriell-Chip nach aussen gefuehrt ist (dieselbe,
// die zum Flashen/fuer den Log-Monitor dient) - natives USB-CDC gibt es nur
// auf S3/C3, nicht auf dem klassischen ESP32 (CYD-Board). Der zusaetzliche
// UART-Treiber hier liest nur RX; die bestehende Log-Ausgabe (TX, per
// esp_log/vprintf direkt auf den UART-Registern) laeuft unveraendert weiter.
#define SERIAL_UART_PORT   UART_NUM_0
#define SERIAL_BAUD_RATE   115200
#define SERIAL_RX_BUF_SIZE 2048
#define LINE_BUF_SIZE      512

// Liest byteweise und wertet nach jedem '\n' die gesammelte Zeile als JSON
// aus (gleiches Payload-Format wie MQTT, siehe hw_data.h). Kein Framing/
// Prefix noetig, da auf dieser Leitung im RX-Pfad ausschliesslich der
// PC-Client schreibt.
static void serial_rx_task(void *arg)
{
    static char line[LINE_BUF_SIZE];
    size_t line_len = 0;
    uint8_t byte;

    while (1) {
        int n = uart_read_bytes(SERIAL_UART_PORT, &byte, 1, pdMS_TO_TICKS(1000));
        if (n <= 0) continue;

        if (byte == '\n') {
            if (line_len > 0) {
                hw_data_apply_json(line, line_len);
                serial_connected = true;
                line_len = 0;
            }
            continue;
        }
        if (byte == '\r') continue;

        if (line_len < LINE_BUF_SIZE - 1) {
            line[line_len++] = (char)byte;
        } else {
            // Zeile zu lang (z.B. falsche Baudrate/Muell) - verwerfen statt
            // ueberlaufen zu lassen, auf die naechste Zeile warten.
            line_len = 0;
        }
    }
}

void serial_handler_begin(void)
{
    uart_config_t cfg = {
        .baud_rate  = SERIAL_BAUD_RATE,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_param_config(SERIAL_UART_PORT, &cfg));
    ESP_ERROR_CHECK(uart_driver_install(SERIAL_UART_PORT, SERIAL_RX_BUF_SIZE, 0, 0, NULL, 0));

    xTaskCreate(serial_rx_task, "serial_hw", 4096, NULL, 5, NULL);
    ESP_LOGI(TAG, "USB/Seriell-Empfang gestartet (UART%d, %d Baud)", SERIAL_UART_PORT, SERIAL_BAUD_RATE);
}
