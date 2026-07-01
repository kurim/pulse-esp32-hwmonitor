#include "touch_xpt2046.h"
#include "bsp_pins.h"
#include "shared_state.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "touch";
static spi_device_handle_t s_spi;

// Roh-ADC-Bereich (12 Bit) - Werksstreuung moeglich. Wenn Antippen an den
// Raendern nicht anspricht oder "wandert", hier nachjustieren.
#define TOUCH_RAW_X_MIN 200
#define TOUCH_RAW_X_MAX 3700
#define TOUCH_RAW_Y_MIN 240
#define TOUCH_RAW_Y_MAX 3800

// XPT2046-Kommandobytes (12-Bit, differenziell)
#define CMD_READ_X 0xD0
#define CMD_READ_Y 0x90

static int map_range(int v, int in_min, int in_max, int out_min, int out_max)
{
    if (in_max == in_min) return out_min;
    return (v - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

static int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

void touch_xpt2046_init(void)
{
    spi_bus_config_t buscfg = {
        .mosi_io_num     = TOUCH_PIN_MOSI,
        .miso_io_num     = TOUCH_PIN_MISO,
        .sclk_io_num     = TOUCH_PIN_CLK,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = 32,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(TOUCH_SPI_HOST, &buscfg, SPI_DMA_DISABLED));

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = TOUCH_SPI_HZ,
        .mode           = 0,
        .spics_io_num   = TOUCH_PIN_CS,
        .queue_size     = 1,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(TOUCH_SPI_HOST, &devcfg, &s_spi));

    // IRQ-Pin (PENIRQ, aktiv LOW) als Eingang zur Beruehrungserkennung.
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << TOUCH_PIN_IRQ,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&io);

    ESP_LOGI(TAG, "XPT2046 initialisiert (SPI%d)", TOUCH_SPI_HOST + 1);
}

// Liest einen 12-Bit-Wert des angegebenen Kanals.
static int read_channel(uint8_t cmd)
{
    uint8_t tx[3] = { cmd, 0x00, 0x00 };
    uint8_t rx[3] = { 0 };
    spi_transaction_t t = {
        .length    = 3 * 8,
        .tx_buffer = tx,
        .rx_buffer = rx,
    };
    if (spi_device_polling_transmit(s_spi, &t) != ESP_OK) return -1;
    return ((rx[1] << 8) | rx[2]) >> 3; // 12 Bit
}

bool touch_xpt2046_read(uint16_t *sx, uint16_t *sy)
{
    // PENIRQ LOW = beruehrt.
    if (gpio_get_level(TOUCH_PIN_IRQ) != 0) return false;

    // Mehrere Messungen mitteln (rauschunterdrueckend).
    const int N = 4;
    int accx = 0, accy = 0;
    for (int i = 0; i < N; i++) {
        accx += read_channel(CMD_READ_X);
        accy += read_channel(CMD_READ_Y);
    }
    int rx = clampi(accx / N, TOUCH_RAW_X_MIN, TOUCH_RAW_X_MAX);
    int ry = clampi(accy / N, TOUCH_RAW_Y_MIN, TOUCH_RAW_Y_MAX);

    // Mapping analog zum Arduino-Port. Achsen sind gegenueber dem Display
    // vertauscht (rx/ry gekreuzt). HINWEIS: Je nach Board-Charge koennen X/Y
    // vertauscht oder invertiert sein - dann die Faelle hier anpassen.
    int x, y;
    switch (app_config.rotation) {
        case 1: // Landscape, USB rechts (Default)
            x = map_range(ry, TOUCH_RAW_Y_MIN, TOUCH_RAW_Y_MAX, 320, 0);
            y = map_range(rx, TOUCH_RAW_X_MIN, TOUCH_RAW_X_MAX, 240, 0);
            break;
        case 3: // Landscape, USB links (180 Grad gegenueber rotation=1)
            x = map_range(ry, TOUCH_RAW_Y_MIN, TOUCH_RAW_Y_MAX, 320, 0);
            y = map_range(rx, TOUCH_RAW_X_MIN, TOUCH_RAW_X_MAX, 240, 0);
            break;
        case 0: // Portrait
            x = map_range(rx, TOUCH_RAW_X_MIN, TOUCH_RAW_X_MAX, 0, 240);
            y = map_range(ry, TOUCH_RAW_Y_MIN, TOUCH_RAW_Y_MAX, 0, 320);
            break;
        default: // 2, Portrait gedreht
            x = map_range(rx, TOUCH_RAW_X_MIN, TOUCH_RAW_X_MAX, 240, 0);
            y = map_range(ry, TOUCH_RAW_Y_MIN, TOUCH_RAW_Y_MAX, 320, 0);
            break;
    }

    *sx = (uint16_t)clampi(x, 0, TFT_H_RES - 1);
    *sy = (uint16_t)clampi(y, 0, TFT_V_RES - 1);
    return true;
}
