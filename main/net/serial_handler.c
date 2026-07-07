#include "serial_handler.h"
#include "shared_state.h"
#include "hw_data.h"
#include "sdkconfig.h"
#include "soc/soc_caps.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

#if SOC_USB_SERIAL_JTAG_SUPPORTED
#include "driver/usb_serial_jtag.h"
#endif

static const char *TAG = "serial";

#define SERIAL_UART_PORT   UART_NUM_0
#define SERIAL_BAUD_RATE   115200
#define SERIAL_RX_BUF_SIZE 2048
#define LINE_BUF_SIZE      512

// true, sobald das USB-Serial/JTAG-Backend aktiv ist (statt UART0) - von
// serial_handler_begin() gesetzt, von serial_rx_task() beim Lesen abgefragt.
#if SOC_USB_SERIAL_JTAG_SUPPORTED
static bool s_use_usb_jtag = false;
#endif

// Liest genau ein Byte vom aktiven Backend (UART0 oder natives USB-Serial/
// JTAG, siehe serial_handler_begin()). Rueckgabewert wie uart_read_bytes/
// usb_serial_jtag_read_bytes: Anzahl gelesener Bytes (0 bei Timeout, <0 bei
// Fehler).
static int serial_read_byte(uint8_t *byte)
{
#if SOC_USB_SERIAL_JTAG_SUPPORTED
    if (s_use_usb_jtag) {
        return usb_serial_jtag_read_bytes(byte, 1, pdMS_TO_TICKS(1000));
    }
#endif
    return uart_read_bytes(SERIAL_UART_PORT, byte, 1, pdMS_TO_TICKS(1000));
}

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
        int n = serial_read_byte(&byte);
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
#if SOC_USB_SERIAL_JTAG_SUPPORTED
    if (app_config.serial_iface == SERIAL_IFACE_USB_JTAG) {
        // Wenn die Konsole ueber USB-Serial/JTAG laeuft (CONFIG_ESP_CONSOLE_
        // USB_SERIAL_JTAG), hat ESP-IDF diesen Treiber beim Boot bereits fuer
        // die Log-Ausgabe installiert - ein zweiter Install-Call liefert dann
        // ESP_ERR_INVALID_STATE. Das ist hier kein Fehler (wir lesen einfach
        // vom bereits laufenden Treiber), nur andere Fehler sind fatal.
        usb_serial_jtag_driver_config_t jtag_cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
        esp_err_t err = usb_serial_jtag_driver_install(&jtag_cfg);
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            ESP_ERROR_CHECK(err);
        }
        s_use_usb_jtag = true;
        xTaskCreate(serial_rx_task, "serial_hw", 4096, NULL, 5, NULL);
        ESP_LOGI(TAG, "USB-Seriell-Empfang gestartet (natives USB-Serial-JTAG, kein Bridgechip)");
        return;
    }
#endif

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
