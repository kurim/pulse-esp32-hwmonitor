#include "serial_handler.h"
#include "shared_state.h"
#include "hw_data.h"
#include <Arduino.h>

#define SERIAL_BAUD_RATE 115200
#define LINE_BUF_SIZE    512

static char   s_line[LINE_BUF_SIZE];
static size_t s_line_len;
static bool   s_enabled;

void serial_handler_begin(void)
{
    // Serial (UART0) ist von main.cpp bereits fuer Log-Ausgabe mit
    // SERIAL_BAUD_RATE offen - hier nur die Nutzung als Datenquelle markieren.
    s_enabled = true;
}

// Liest byteweise und wertet nach jedem '\n' die gesammelte Zeile als JSON
// aus (gleiches Payload-Format wie MQTT, siehe hw_data.h).
void serial_handler_loop(void)
{
    if (!s_enabled) return;

    while (Serial.available() > 0) {
        char c = (char)Serial.read();

        if (c == '\n') {
            if (s_line_len > 0) {
                hw_data_apply_json(s_line, s_line_len);
                serial_connected = true;
                s_line_len = 0;
            }
            continue;
        }
        if (c == '\r') continue;

        if (s_line_len < LINE_BUF_SIZE - 1) {
            s_line[s_line_len++] = c;
        } else {
            // Zeile zu lang (z.B. falsche Baudrate/Muell) - verwerfen.
            s_line_len = 0;
        }
    }
}
