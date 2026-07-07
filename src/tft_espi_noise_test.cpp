// Isolierter TFT_eSPI-Vergleichstest fuer das CYD-Rauschproblem (siehe
// display_ui.cpp/lgfx_profiles.h fuer die LovyanGFX-Seite der Fehlersuche).
// Nur aktiv im PlatformIO-Environment "esp32_tftespi_test"
// (-DTFT_ESPI_NOISE_TEST, siehe platformio.ini) - ersetzt dort komplett
// setup()/loop() aus main.cpp (dort per #ifndef ausgeblendet), damit LVGL/
// WLAN/restlicher App-Code keine Nebeneffekte einbringen koennen.
//
// Zweck: dieselben Streifentests wie in display_ui.cpp (Stufe A: viele
// kleine 20-Zeilen-Chunks, Stufe B: wenige grosse 60-Zeilen-Chunks), aber
// komplett unabhaengig von LovyanGFX ueber TFT_eSPI erzeugt. Kommt das
// Rauschen hier NICHT vor, liegt der Fehler in LovyanGFX/unserer Nutzung
// davon. Kommt es genauso vor, ist es kein LovyanGFX-spezifischer Bug,
// sondern etwas Grundsaetzlicheres (Hardware/Board/ESP-IDF-SPI-Treiber).
#ifdef TFT_ESPI_NOISE_TEST

#include <Arduino.h>
#include <TFT_eSPI.h>

static TFT_eSPI tft = TFT_eSPI();

static void stripe_test(uint32_t lines_per_group, const char *label)
{
    const uint32_t w = tft.width();
    const uint32_t h_total = tft.height();
    static const uint16_t bar_colors[] = {0xF800, 0x07E0, 0x001F, 0xFFE0, 0xF81F, 0x07FF};
    const uint32_t n_colors = sizeof(bar_colors) / sizeof(bar_colors[0]);
    const uint32_t lines_per_stripe = 20;

    uint16_t *buf = (uint16_t *)malloc(w * lines_per_group * sizeof(uint16_t));
    if (!buf) {
        Serial.printf("TFT_eSPI-Test %s: malloc(%u) fehlgeschlagen\n",
                       label, (unsigned)(w * lines_per_group * sizeof(uint16_t)));
        return;
    }

    tft.startWrite();
    uint32_t n_calls = 0;
    for (uint32_t y = 0; y < h_total; y += lines_per_group, n_calls++) {
        uint32_t h = (h_total - y < lines_per_group) ? (h_total - y) : lines_per_group;
        for (uint32_t row = 0; row < h; row++) {
            uint16_t col = bar_colors[((y + row) / lines_per_stripe) % n_colors];
            for (uint32_t x = 0; x < w; x++) buf[row * w + x] = col;
        }
        tft.setAddrWindow(0, y, w, h);
        tft.pushColors(buf, w * h, false);
    }
    tft.endWrite();
    Serial.printf("TFT_eSPI-Test %s: %u setAddrWindow-Aufrufe a %u Zeilen\n",
                   label, (unsigned)n_calls, (unsigned)lines_per_group);
    free(buf);
    delay(4000);
}

void setup(void)
{
    Serial.begin(115200);
    delay(300);
    Serial.println("=== TFT_eSPI Rausch-Vergleichstest (temporaer) ===");

    tft.init();
    tft.setRotation(1); // Landscape 320x240, wie der CYD-Dashboard-Betrieb
    tft.setSwapBytes(false);

    tft.fillScreen(TFT_RED);   delay(1500);
    tft.fillScreen(TFT_GREEN); delay(1500);
    tft.fillScreen(TFT_BLUE);  delay(1500);
    tft.fillScreen(TFT_WHITE); delay(1500);

    stripe_test(20, "A (20 Zeilen)");
    stripe_test(60, "B (60 Zeilen)");

    Serial.println("TFT_eSPI-Test fertig.");
}

void loop(void)
{
    delay(1000);
}

#endif // TFT_ESPI_NOISE_TEST
