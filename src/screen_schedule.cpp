#include "screen_schedule.h"
#include "shared_state.h"
#include "time_service.h"
#include <Arduino.h>
#include <lvgl.h>
#include <string.h>
#include <strings.h>

static constexpr uint32_t kWakeMs      = 30000;
static constexpr uint32_t kPollMs      = 100;
static constexpr uint32_t kTouchSeenMs = 400; // LVGL-Inaktivitaet darunter = gerade angetippt

static ScreenOverride    s_override = SCREEN_OVERRIDE_AUTO;
static volatile uint32_t s_activityMs;       // 0 = noch keine BOOT-Taste
static uint32_t          s_wakeUntilMs;      // 0 = nicht geweckt
static bool              s_isOff;
static bool              s_applied;          // wurde der Zustand schon einmal geschrieben?
static lv_obj_t         *s_overlay;
static uint32_t          s_lastPollMs;

bool screen_schedule_is_off(const struct tm &t)
{
    if (!app_config.screen_sched_enabled) return false;

    // tm_wday: 0 = Sonntag -> Index 0 = Montag.
    int today = (t.tm_wday + 6) % 7;
    int yday  = (today + 6) % 7;
    int m     = t.tm_hour * 60 + t.tm_min;

    // Ganze-Woche-Modus: Eintrag 0 ist die Vorlage fuer alle Tage.
    const bool week = app_config.screen_sched_mode == 0;
    const screen_sched_day_t &d_today = app_config.screen_sched_day[week ? 0 : today];
    const screen_sched_day_t &d_yday  = app_config.screen_sched_day[week ? 0 : yday];

    if (d_today.enabled && d_today.off_min != d_today.on_min) {
        if (d_today.off_min < d_today.on_min) {
            if (m >= d_today.off_min && m < d_today.on_min) return true;
        } else if (m >= d_today.off_min) {
            return true; // Fenster ueber Mitternacht: Abendteil
        }
    }
    // Morgenteil eines gestern begonnenen Fensters.
    if (d_yday.enabled && d_yday.off_min > d_yday.on_min && m < d_yday.on_min) return true;
    return false;
}

void screen_schedule_set_override(ScreenOverride o)
{
    s_override = o;
    s_wakeUntilMs = 0;
}

bool screen_schedule_set_override_text(const char *payload, size_t len)
{
    char buf[8];
    if (len == 0 || len >= sizeof(buf)) return false;
    memcpy(buf, payload, len);
    buf[len] = '\0';
    if (!strcasecmp(buf, "on"))   { screen_schedule_set_override(SCREEN_OVERRIDE_ON);   return true; }
    if (!strcasecmp(buf, "off"))  { screen_schedule_set_override(SCREEN_OVERRIDE_OFF);  return true; }
    if (!strcasecmp(buf, "auto")) { screen_schedule_set_override(SCREEN_OVERRIDE_AUTO); return true; }
    return false;
}

void screen_schedule_notify_activity(void)
{
    s_activityMs = millis() | 1; // 0 bleibt "nie"
}

void screen_schedule_apply_config(void)
{
    s_applied = false; // naechster poll() schreibt den Zustand neu
    s_lastPollMs = 0;
}

static void set_screen(bool off)
{
    // Schwarze Abdeckung ueber allem: einziger Weg, auf Boards ohne
    // schaltbaren Backlight-Pin (und auf OLEDs) trotzdem dunkel zu werden.
    // Auf Backlight-Boards verhindert sie zusaetzlich kurzes Aufblitzen.
    if (!s_overlay) {
        s_overlay = lv_obj_create(lv_layer_top());
        lv_obj_remove_style_all(s_overlay);
        lv_obj_set_size(s_overlay, LV_PCT(100), LV_PCT(100));
        lv_obj_set_style_bg_color(s_overlay, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(s_overlay, LV_OPA_COVER, 0);
        lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
    }
    if (off) lv_obj_remove_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
    else     lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);

#ifdef BOARD_HAS_BACKLIGHT_PWM
    boardDisplaySetBrightness(off ? 0 : app_config.brightness);
#else
    boardDisplayBacklight(!off);
#endif
}

void screen_schedule_poll(void)
{
    uint32_t now = millis();
    if (s_applied && now - s_lastPollMs < kPollMs) return;
    s_lastPollMs = now;

    bool wantOff = false;
    if (s_override == SCREEN_OVERRIDE_OFF) {
        wantOff = true;
    } else if (s_override == SCREEN_OVERRIDE_AUTO && app_config.screen_sched_enabled &&
               time_service_is_synced()) {
        time_t tt = time(nullptr);
        struct tm t;
        localtime_r(&tt, &t);
        wantOff = screen_schedule_is_off(t);
    }

    // Wecken: Antippen (LVGL-Inaktivitaetszeit) oder BOOT-Taste. Nur relevant,
    // solange dunkel - sonst wuerde jedes Tippen im laufenden Betrieb ein
    // 30-s-Fenster in die naechste Dunkelphase hineinziehen.
    if (wantOff && s_isOff) {
        bool touched = lv_display_get_inactive_time(nullptr) < kTouchSeenMs;
        bool button  = s_activityMs && (now - s_activityMs) < kTouchSeenMs;
        if (touched || button) s_wakeUntilMs = now + kWakeMs;
    }
    if (!wantOff) s_wakeUntilMs = 0;
    if (s_wakeUntilMs && (int32_t)(now - s_wakeUntilMs) >= 0) s_wakeUntilMs = 0;
    if (s_wakeUntilMs) wantOff = false;

    if (s_applied && wantOff == s_isOff) return;
    s_applied = true;
    s_isOff   = wantOff;
    set_screen(wantOff);
}
