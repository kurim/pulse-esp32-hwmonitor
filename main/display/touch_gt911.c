#include "touch_gt911.h"
#include "shared_state.h"

#include "driver/i2c_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_log.h"

static const char *TAG = "touch_gt911";

esp_lcd_touch_handle_t touch_gt911_init(const board_profile_t *profile)
{
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = profile->i2c_sda,
        .scl_io_num = profile->i2c_scl,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus = NULL;
    esp_err_t err = i2c_new_master_bus(&bus_cfg, &bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_new_master_bus: %s", esp_err_to_name(err));
        return NULL;
    }

    esp_lcd_panel_io_i2c_config_t io_cfg = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
    io_cfg.scl_speed_hz = profile->i2c_hz;
    io_cfg.dev_addr = (uint8_t)profile->i2c_addr;

    esp_lcd_panel_io_handle_t io = NULL;
    err = esp_lcd_new_panel_io_i2c(bus, &io_cfg, &io);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_lcd_new_panel_io_i2c: %s", esp_err_to_name(err));
        return NULL;
    }

    // Rotation 0-3 = alle vier Mirror-Kombinationen, dieselbe Zuordnung wie in
    // display_ui.c: lcd_init_rgb() (muss exakt uebereinstimmen, sonst zeigt
    // Touch auf falsche Koordinaten).
    bool mirror_x = false, mirror_y = false;
    switch (app_config.rotation) {
        case 1:  mirror_x = true;  mirror_y = false; break;
        case 2:  mirror_x = false; mirror_y = true;  break;
        case 3:  mirror_x = true;  mirror_y = true;  break;
        case 0:
        default: mirror_x = false; mirror_y = false; break;
    }

    esp_lcd_touch_config_t tp_cfg = {
        .x_max = profile->h_res,
        .y_max = profile->v_res,
        .rst_gpio_num = profile->touch_rst,
        .int_gpio_num = profile->touch_int,
        // reset=0: der esp_lcd_touch-Reset-Ablauf legt den Pin zuerst auf
        // levels.reset (Reset AKTIV), danach auf !levels.reset (Reset
        // freigegeben) - GT911-Module sind ueblicherweise aktiv-LOW resettet.
        // Der vorherige Wert (1, aus einem GT1151-Beispiel uebernommen, dort
        // ohne echten RST-Pin getestet) hielt den Chip vermutlich dauerhaft im
        // Reset - erklaert, warum Touch bisher gar nicht reagierte.
        .levels = { .reset = 0, .interrupt = 0 },
        .flags = { .swap_xy = 0, .mirror_x = mirror_x, .mirror_y = mirror_y },
    };

    esp_lcd_touch_handle_t tp = NULL;
    err = esp_lcd_touch_new_i2c_gt911(io, &tp_cfg, &tp);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_lcd_touch_new_i2c_gt911: %s", esp_err_to_name(err));
        return NULL;
    }
    return tp;
}
