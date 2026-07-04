#include "web_portal.h"
#include "shared_state.h"
#include "config_store.h"
#include "board_profiles.h"

#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_ota_ops.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "cJSON.h"
#include "lwip/sockets.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "web";

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
#define STA_MAX_RETRY      8
#define STA_TIMEOUT_MS     15000

static EventGroupHandle_t s_wifi_events;
static int  s_retry;
static bool s_ap_mode;
static char s_ip[16]   = "0.0.0.0";
static char s_ap_ssid[24];
static httpd_handle_t s_httpd;

// ------------------------------------------------------------------
// Eingebettete Config-Seite (identisch zum Arduino-Port; OTA-Upload sendet
// die .bin roh als Request-Body, damit der esp_http_server sie direkt an
// esp_ota_write() weiterreichen kann).
// ------------------------------------------------------------------
static const char INDEX_HTML[] =
"<!DOCTYPE html><html lang=\"de\"><head><meta charset=\"UTF-8\">"
"<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
"<title>Pulse ESP32 Hardware-Monitor</title><style>"
":root{--bg:#10141c;--card:#1a2030;--accent:#3fd0e0;--text:#e8edf4;--sub:#8893a8;--border:#2a3142;}"
"*{box-sizing:border-box;}body{background:var(--bg);color:var(--text);font-family:-apple-system,Segoe UI,Roboto,Arial,sans-serif;margin:0;padding:24px 16px;}"
".wrap{max-width:560px;margin:0 auto;}h1{font-size:1.4rem;margin-bottom:4px;}"
".sub{color:var(--sub);margin-bottom:24px;font-size:.9rem;}"
".card{background:var(--card);border:1px solid var(--border);border-radius:10px;padding:18px 20px;margin-bottom:16px;}"
".card h2{font-size:1rem;margin:0 0 14px;color:var(--accent);}"
"label{display:block;font-size:.82rem;color:var(--sub);margin:10px 0 4px;}"
"input[type=text],input[type=password],input[type=number],select{width:100%;padding:9px 10px;border-radius:6px;border:1px solid var(--border);background:#0d111a;color:var(--text);font-size:.95rem;}"
".row{display:flex;gap:12px;}.row>div{flex:1;}"
".toggle{display:flex;align-items:center;gap:10px;margin:10px 0;}"
"button{background:var(--accent);color:#04222a;border:none;border-radius:8px;padding:12px 18px;font-size:1rem;font-weight:600;cursor:pointer;width:100%;margin-top:6px;}"
"button:hover{opacity:.9;}#status{margin-top:14px;font-size:.85rem;color:var(--sub);}"
".statbar{display:flex;gap:16px;font-size:.85rem;color:var(--sub);margin-bottom:18px;flex-wrap:wrap;}"
".dot{display:inline-block;width:8px;height:8px;border-radius:50%;margin-right:6px;}"
".pintable{width:100%;border-collapse:collapse;margin-top:10px;font-size:.85rem;}"
".pintable td{padding:3px 6px;border-bottom:1px solid var(--border);}"
".pintable td:first-child{color:var(--sub);}"
".hint{font-size:.8rem;color:var(--sub);margin-top:8px;}"
// Klickbare Liste statt <datalist> - <datalist>-Vorschlaege werden von
// mobilen Browsern (Android Chrome/iOS Safari) inkonsistent bis gar nicht
// angezeigt, eine simple Liste aus Buttons funktioniert dagegen ueberall.
".wifilist{margin-top:8px;border:1px solid var(--border);border-radius:8px;overflow:hidden;max-height:220px;overflow-y:auto;}"
".wifilist button{width:100%;text-align:left;background:#0d111a;color:var(--text);border:none;border-radius:0;"
"border-bottom:1px solid var(--border);padding:10px 12px;margin:0;font-size:.9rem;font-weight:400;display:flex;justify-content:space-between;gap:10px;}"
".wifilist button:last-child{border-bottom:none;}"
".wifilist button:active,.wifilist button:hover{background:#161c29;}"
".wifilist .rssi{color:var(--sub);font-size:.8rem;white-space:nowrap;}"
"</style></head><body><div class=\"wrap\">"
"<h1>Pulse ESP32 Hardware-Monitor</h1>"
"<div class=\"sub\"><span data-i18n=\"sub_title\">Konfiguration (ESP-IDF)</span> "
"<select id=\"langSwitch\" style=\"width:auto;display:inline-block;padding:2px 6px;font-size:.8rem;margin-left:6px\">"
"<option value=\"de\">Deutsch</option><option value=\"en\">English</option></select></div>"
"<div class=\"statbar\" id=\"livebar\" data-i18n=\"loading_status\">Lade Status...</div>"
"<form id=\"wifiForm\"><div class=\"card\"><h2 data-i18n=\"card_wifi\">WLAN</h2>"
"<label>SSID</label><input type=\"text\" id=\"wifi_ssid\" name=\"wifi-ssid\" maxlength=\"32\" autocomplete=\"off\">"
"<button type=\"button\" onclick=\"scanWifi()\" data-i18n=\"btn_scan_wifi\" style=\"background:var(--card);color:var(--text);border:1px solid var(--border);margin-top:8px\">WLAN-Netzwerke suchen</button>"
"<div class=\"hint\" id=\"wifiScanStatus\"></div>"
"<div class=\"wifilist\" id=\"wifiList\" style=\"display:none\"></div>"
"<label data-i18n=\"lbl_password\">Passwort</label><input type=\"password\" id=\"wifi_pass\" name=\"wifi-password\" maxlength=\"64\" autocomplete=\"new-password\" data-i18n-ph=\"ph_keep_empty\" placeholder=\"unver&auml;ndert lassen = leer\">"
"<button type=\"submit\" data-i18n=\"btn_save_wifi\">WLAN speichern &amp; Neustart</button><div id=\"wifiStatus\"></div></div></form>"
"<button type=\"button\" id=\"advToggle\" onclick=\"toggleAdvanced()\" data-i18n=\"show_advanced\" style=\"background:var(--card);color:var(--text);border:1px solid var(--border)\">Erweiterte Einstellungen anzeigen</button>"
"<div id=\"advancedWrap\" style=\"display:none;margin-top:16px\">"
"<form id=\"cfgForm\">"
"<div class=\"card\"><h2 data-i18n=\"card_mqtt\">MQTT (Hardwaredaten vom PC)</h2><label>Broker-Host</label><input type=\"text\" id=\"mqtt_host\" maxlength=\"64\">"
"<div class=\"row\"><div><label>Port</label><input type=\"number\" id=\"mqtt_port\" min=\"1\" max=\"65535\"></div>"
"<div><label>Topic</label><input type=\"text\" id=\"mqtt_topic\" maxlength=\"64\"></div></div>"
"<div class=\"row\"><div><label data-i18n=\"lbl_username\">Benutzer</label><input type=\"text\" id=\"mqtt_user\" maxlength=\"32\"></div>"
"<div><label data-i18n=\"lbl_password\">Passwort</label><input type=\"password\" id=\"mqtt_pass\" maxlength=\"64\" data-i18n-ph=\"ph_keep_empty\" placeholder=\"unver&auml;ndert lassen = leer\"></div></div></div>"
"<div class=\"card\"><h2 data-i18n=\"card_time\">Zeit</h2><label>NTP-Server</label><input type=\"text\" id=\"ntp_server\" maxlength=\"64\">"
"<label data-i18n=\"lbl_posix_tz\">POSIX-Zeitzone</label><input type=\"text\" id=\"tz\" maxlength=\"64\"></div>"
"<div class=\"card\"><h2 data-i18n=\"card_weather\">Wetter (optional, OpenWeatherMap)</h2>"
"<div class=\"toggle\"><input type=\"checkbox\" id=\"weather_enabled\"><label style=\"margin:0\" data-i18n=\"lbl_enable\">Aktivieren</label></div>"
"<label>API-Key</label><input type=\"text\" id=\"weather_api_key\" maxlength=\"40\" data-i18n-ph=\"ph_keep_empty\" placeholder=\"unver&auml;ndert lassen = leer\">"
"<div class=\"row\"><div><label data-i18n=\"lbl_location\">Ort (Stadt,Land)</label><input type=\"text\" id=\"weather_city\" maxlength=\"64\"></div>"
"<div><label data-i18n=\"lbl_unit\">Einheit</label><select id=\"weather_units\"><option value=\"metric\">&deg;C</option><option value=\"imperial\">&deg;F</option></select></div></div></div>"
"<div class=\"card\"><h2 data-i18n=\"card_display\">Display</h2>"
"<div id=\"displayFixed\" class=\"sub\" style=\"display:none;margin-bottom:6px\"></div>"
"<div id=\"displayChoice\"><label data-i18n=\"lbl_display_type\">Displaytyp</label><select id=\"display_type\"></select></div>"
"<div id=\"displayInfo\"></div>"
"<div class=\"hint\" data-i18n=\"hint_display_reboot\">Nach dem Speichern startet das Ger&auml;t neu und initialisiert das gew&auml;hlte Panel. "
"Verdrahtung wie oben angezeigt oder weiter unten die PINs anpassen.</div>"
"<div class=\"row\" style=\"margin-top:10px\">"
"<div><label data-i18n=\"lbl_brightness\">Helligkeit (0-255)</label><input type=\"number\" id=\"brightness\" min=\"0\" max=\"255\"></div>"
"<div><label data-i18n=\"lbl_rotation\">Rotation/Spiegelung</label><select id=\"rotation\"></select></div></div>"
"<div class=\"row\"><div><label data-i18n=\"lbl_language\">Sprache (Ger&auml;t-Display)</label>"
"<select id=\"language\"><option value=\"de\">Deutsch</option><option value=\"en\">English</option></select></div><div></div></div>"
"<label data-i18n=\"lbl_standby\">Standby nach (Sekunden ohne MQTT-Daten, 0 = deaktiviert)</label>"
"<input type=\"number\" id=\"standby_timeout_s\" min=\"0\" max=\"65535\">"
"<div class=\"toggle\"><input type=\"checkbox\" id=\"color_invert\"><label style=\"margin:0\" data-i18n=\"lbl_invert\">Farben invertieren (Dark Mode, falls Hintergrund hell statt dunkel ist)</label></div></div>"
"<div class=\"card\" id=\"pinCard\"><h2 data-i18n=\"card_pins\">Pin-Belegung (optional anpassen)</h2>"
"<div class=\"hint\" data-i18n=\"hint_pins\">Leer lassen = Standard-Pin f&uuml;r den oben gew&auml;hlten Displaytyp verwenden (siehe Tabelle "
"oben). Nur bei abweichender eigener Verdrahtung &auml;ndern. Wechselt der Displaytyp, werden alle Pin-Overrides "
"zur&uuml;ckgesetzt.</div>"
"<div id=\"pinGroupSpi\" style=\"margin-top:10px\">"
"<div class=\"row\"><div><label>MOSI</label><input type=\"number\" id=\"pin_mosi\" min=\"0\" max=\"48\"></div>"
"<div><label data-i18n=\"lbl_miso\">MISO (-1 = kein Pin)</label><input type=\"number\" id=\"pin_miso\" min=\"-1\" max=\"48\"></div></div>"
"<div class=\"row\"><div><label>SCLK</label><input type=\"number\" id=\"pin_sclk\" min=\"0\" max=\"48\"></div>"
"<div><label>CS</label><input type=\"number\" id=\"pin_cs\" min=\"0\" max=\"48\"></div></div>"
"<div class=\"row\"><div><label>DC</label><input type=\"number\" id=\"pin_dc\" min=\"0\" max=\"48\"></div>"
"<div><label data-i18n=\"lbl_reset_pin\">RESET (-1 = kein Pin)</label><input type=\"number\" id=\"pin_rst\" min=\"-1\" max=\"48\"></div></div>"
"<div class=\"row\"><div><label data-i18n=\"lbl_backlight_pin\">Backlight (-1 = kein Pin)</label><input type=\"number\" id=\"pin_bl\" min=\"-1\" max=\"48\"></div>"
"<div></div></div></div>"
"<div id=\"pinGroupTouch\">"
"<div class=\"row\"><div><label>Touch CS</label><input type=\"number\" id=\"pin_touch_cs\" min=\"0\" max=\"48\"></div>"
"<div><label>Touch IRQ</label><input type=\"number\" id=\"pin_touch_irq\" min=\"0\" max=\"48\"></div></div>"
"<div class=\"row\"><div><label>Touch MOSI</label><input type=\"number\" id=\"pin_touch_mosi\" min=\"0\" max=\"48\"></div>"
"<div><label>Touch MISO</label><input type=\"number\" id=\"pin_touch_miso\" min=\"0\" max=\"48\"></div></div>"
"<div class=\"row\"><div><label>Touch CLK</label><input type=\"number\" id=\"pin_touch_clk\" min=\"0\" max=\"48\"></div>"
"<div></div></div></div>"
"<div id=\"pinGroupI2c\">"
"<div class=\"row\"><div><label>SDA</label><input type=\"number\" id=\"pin_i2c_sda\" min=\"0\" max=\"48\"></div>"
"<div><label>SCL</label><input type=\"number\" id=\"pin_i2c_scl\" min=\"0\" max=\"48\"></div></div>"
"<label data-i18n=\"lbl_i2c_addr\">I2C-Adresse (dezimal, z.B. 60 f&uuml;r 0x3C)</label><input type=\"number\" id=\"pin_i2c_addr\" min=\"1\" max=\"127\"></div>"
"<div id=\"pinGroupNav\">"
"<label data-i18n=\"lbl_nav_button\">Navigationstaste (BOOT-Button, -1 = keine)</label><input type=\"number\" id=\"pin_nav_button\" min=\"-1\" max=\"48\"></div>"
"</div>"
"<button type=\"submit\" data-i18n=\"btn_save_cfg\">Speichern &amp; Neustart</button><div id=\"status\"></div></form>"
"<div class=\"card\"><h2 data-i18n=\"card_ota\">Firmware-Update (OTA)</h2>"
"<div class=\"sub\" style=\"margin-bottom:10px\"><span data-i18n=\"lbl_current_version\">Aktuelle Version:</span> <span id=\"fwVersion\">-</span></div>"
"<input type=\"file\" id=\"fwFile\" accept=\".bin\">"
"<button type=\"button\" onclick=\"uploadFirmware()\" data-i18n=\"btn_upload_fw\" style=\"background:#e0a53f\">.bin hochladen &amp; flashen</button>"
"<progress id=\"otaProgress\" value=\"0\" max=\"100\" style=\"width:100%;margin-top:10px;display:none\"></progress>"
"<div id=\"otaStatus\" style=\"margin-top:8px;font-size:.85rem;color:var(--sub)\"></div></div>"
"</div></div>"
"<script>"
// I18N: reine Client-seitige WebUI-Sprache (Dropdown + Browser-Erkennung +
// localStorage), unabhaengig vom Geraete-Display-Sprachfeld weiter unten im
// Formular ("language" - das steuert main/ui_strings.c auf dem Geraet und
// braucht einen Neustart, siehe app_config.language).
"const I18N={"
"de:{sub_title:'Konfiguration (ESP-IDF)',loading_status:'Lade Status...',card_wifi:'WLAN',"
"btn_scan_wifi:'WLAN-Netzwerke suchen',lbl_password:'Passwort',ph_keep_empty:'unver\\u00e4ndert lassen = leer',"
"btn_save_wifi:'WLAN speichern & Neustart',show_advanced:'Erweiterte Einstellungen anzeigen',"
"hide_advanced:'Erweiterte Einstellungen ausblenden',card_mqtt:'MQTT (Hardwaredaten vom PC)',"
"lbl_username:'Benutzer',card_time:'Zeit',lbl_posix_tz:'POSIX-Zeitzone',card_weather:'Wetter (optional, OpenWeatherMap)',"
"lbl_enable:'Aktivieren',lbl_location:'Ort (Stadt,Land)',lbl_unit:'Einheit',card_display:'Display',"
"lbl_display_type:'Displaytyp',hint_display_reboot:'Nach dem Speichern startet das Ger\\u00e4t neu und initialisiert das gew\\u00e4hlte Panel. Verdrahtung wie oben angezeigt oder weiter unten die PINs anpassen.',"
"lbl_brightness:'Helligkeit (0-255)',lbl_rotation:'Rotation/Spiegelung',lbl_language:'Sprache (Ger\\u00e4t-Display)',"
"lbl_standby:'Standby nach (Sekunden ohne MQTT-Daten, 0 = deaktiviert)',lbl_invert:'Farben invertieren (Dark Mode, falls Hintergrund hell statt dunkel ist)',"
"card_pins:'Pin-Belegung (optional anpassen)',hint_pins:'Leer lassen = Standard-Pin f\\u00fcr den oben gew\\u00e4hlten Displaytyp verwenden (siehe Tabelle oben). Nur bei abweichender eigener Verdrahtung \\u00e4ndern. Wechselt der Displaytyp, werden alle Pin-Overrides zur\\u00fcckgesetzt.',"
"lbl_miso:'MISO (-1 = kein Pin)',lbl_reset_pin:'RESET (-1 = kein Pin)',lbl_backlight_pin:'Backlight (-1 = kein Pin)',"
"lbl_i2c_addr:'I2C-Adresse (dezimal, z.B. 60 f\\u00fcr 0x3C)',lbl_nav_button:'Navigationstaste (BOOT-Button, -1 = keine)',"
"btn_save_cfg:'Speichern & Neustart',card_ota:'Firmware-Update (OTA)',lbl_current_version:'Aktuelle Version:',"
"btn_upload_fw:'.bin hochladen & flashen',saving:'Speichere...',saved_restarting:'Gespeichert. Ger\\u00e4t startet neu...',"
"searching:'Suche...',networks_found:'Netzwerke gefunden.',no_networks_found:'Keine Netzwerke gefunden.',"
"search_error:'Fehler bei der Suche.',select_bin_first:'Bitte zuerst eine .bin-Datei ausw\\u00e4hlen.',"
"confirm_flash_prefix:'Firmware \"',confirm_flash_suffix:'\" jetzt aufspielen? Das Ger\\u00e4t startet danach automatisch neu.',"
"uploading:'Lade hoch...',update_success:'Update erfolgreich - Ger\\u00e4t startet neu.',update_fail_prefix:'Fehler beim Update: ',"
"upload_conn_error:'Verbindungsfehler beim Hochladen.',resolution:'Aufl\\u00f6sung',shape_round:' (rund)',shape_mono:' (monochrom)',"
"not_wired:'nicht verdrahtet',no_pin_self_lit:'kein Pin (selbstleuchtend)',i2c_address:'I2C-Adresse',"
"rotated_180:'180\\u00b0 gedreht',touch_also_rotated:' (Touch ebenfalls gedreht)',fixed_wiring:'Fest verbaut',"
"no_display_profile:'Kein Display-Profil f\\u00fcr dieses Firmware-Target verf\\u00fcgbar.',free_memory_suffix:' (freier Speicher: '},"
"en:{sub_title:'Configuration (ESP-IDF)',loading_status:'Loading status...',card_wifi:'WiFi',"
"btn_scan_wifi:'Scan for WiFi networks',lbl_password:'Password',ph_keep_empty:'leave empty = unchanged',"
"btn_save_wifi:'Save WiFi & restart',show_advanced:'Show advanced settings',"
"hide_advanced:'Hide advanced settings',card_mqtt:'MQTT (hardware data from PC)',"
"lbl_username:'Username',card_time:'Time',lbl_posix_tz:'POSIX timezone',card_weather:'Weather (optional, OpenWeatherMap)',"
"lbl_enable:'Enable',lbl_location:'Location (city,country)',lbl_unit:'Unit',card_display:'Display',"
"lbl_display_type:'Display type',hint_display_reboot:'After saving, the device restarts and initializes the selected panel. Wire it as shown above, or adjust the PINs below.',"
"lbl_brightness:'Brightness (0-255)',lbl_rotation:'Rotation/mirroring',lbl_language:'Language (device display)',"
"lbl_standby:'Standby after (seconds without MQTT data, 0 = disabled)',lbl_invert:'Invert colors (dark mode, if background is light instead of dark)',"
"card_pins:'Pin assignment (optional)',hint_pins:'Leave empty = use the default pin for the display type selected above (see table above). Only change for different custom wiring. Changing the display type resets all pin overrides.',"
"lbl_miso:'MISO (-1 = no pin)',lbl_reset_pin:'RESET (-1 = no pin)',lbl_backlight_pin:'Backlight (-1 = no pin)',"
"lbl_i2c_addr:'I2C address (decimal, e.g. 60 for 0x3C)',lbl_nav_button:'Navigation button (BOOT button, -1 = none)',"
"btn_save_cfg:'Save & restart',card_ota:'Firmware update (OTA)',lbl_current_version:'Current version:',"
"btn_upload_fw:'Upload & flash .bin',saving:'Saving...',saved_restarting:'Saved. Device is restarting...',"
"searching:'Searching...',networks_found:'networks found.',no_networks_found:'No networks found.',"
"search_error:'Search failed.',select_bin_first:'Please select a .bin file first.',"
"confirm_flash_prefix:'Flash firmware \"',confirm_flash_suffix:'\" now? The device will restart automatically afterwards.',"
"uploading:'Uploading...',update_success:'Update successful - device is restarting.',update_fail_prefix:'Update failed: ',"
"upload_conn_error:'Connection error during upload.',resolution:'Resolution',shape_round:' (round)',shape_mono:' (monochrome)',"
"not_wired:'not wired',no_pin_self_lit:'no pin (self-lit)',i2c_address:'I2C address',"
"rotated_180:'180\\u00b0 rotated',touch_also_rotated:' (touch also rotated)',fixed_wiring:'Fixed',"
"no_display_profile:'No display profile available for this firmware target.',free_memory_suffix:' (free memory: '}};"
"let currentLang='de';"
"function t(key){return (I18N[currentLang]&&I18N[currentLang][key])||key;}"
"function applyI18n(lang){"
"if(!I18N[lang])lang='de';currentLang=lang;"
"document.documentElement.lang=lang;"
"document.querySelectorAll('[data-i18n]').forEach(function(el){el.textContent=t(el.dataset.i18n);});"
"document.querySelectorAll('[data-i18n-ph]').forEach(function(el){el.placeholder=t(el.dataset.i18nPh);});"
"const adv=document.getElementById('advancedWrap');"
"if(adv)document.getElementById('advToggle').textContent=(adv.style.display==='none')?t('show_advanced'):t('hide_advanced');"
"const langSwitch=document.getElementById('langSwitch');if(langSwitch)langSwitch.value=lang;"
"if(displays.length){renderDisplayInfo();renderRotationOptions();}"
"}"
"let displays=[];"
"function renderDisplayInfo(){"
"const key=document.getElementById('display_type').value;"
"const d=displays.find(x=>x.key===key);const el=document.getElementById('displayInfo');"
"if(!d||d.bus==='none'){el.innerHTML='';return;}"
"let rows='<tr><td>Bus</td><td>'+d.bus.toUpperCase()+(d.has_touch?' + Touch (XPT2046)':'')+'</td></tr>'"
"+'<tr><td>'+t('resolution')+'</td><td>'+d.h_res+'x'+d.v_res+(d.shape==='round'?t('shape_round'):d.shape==='mono'?t('shape_mono'):'')+'</td></tr>';"
"if(d.bus==='spi'){rows+='<tr><td>MOSI/MISO/SCLK</td><td>GPIO'+d.pins.mosi+' / GPIO'+d.pins.miso+' / GPIO'+d.pins.sclk+'</td></tr>'"
"+'<tr><td>CS / DC</td><td>GPIO'+d.pins.cs+' / GPIO'+d.pins.dc+'</td></tr>'"
"+'<tr><td>RESET</td><td>'+(d.pins.rst<0?t('not_wired'):'GPIO'+d.pins.rst)+'</td></tr>'"
"+'<tr><td>Backlight</td><td>'+(d.pins.bl<0?t('no_pin_self_lit'):'GPIO'+d.pins.bl)+'</td></tr>';"
"if(d.has_touch){rows+='<tr><td>Touch CS/IRQ</td><td>GPIO'+d.pins.touch_cs+' / GPIO'+d.pins.touch_irq+'</td></tr>'"
"+'<tr><td>Touch MOSI/MISO/CLK</td><td>GPIO'+d.pins.touch_mosi+' / GPIO'+d.pins.touch_miso+' / GPIO'+d.pins.touch_clk+'</td></tr>';}"
"}else{rows+='<tr><td>SDA / SCL</td><td>GPIO'+d.pins.sda+' / GPIO'+d.pins.scl+'</td></tr>'"
"+'<tr><td>'+t('i2c_address')+'</td><td>0x'+d.pins.addr.toString(16).toUpperCase()+'</td></tr>'"
"+'<tr><td>VCC / GND</td><td>3.3V / GND</td></tr>';}"
"el.innerHTML='<table class=\"pintable\">'+rows+'</table>';}"
"let activeKey='';"
"const PIN_FIELDS=['mosi','miso','sclk','cs','dc','rst','bl','touch_cs','touch_irq','touch_mosi','touch_miso','touch_clk','i2c_sda','i2c_scl','i2c_addr','nav_button'];"
"const PIN_KEY_MAP={i2c_sda:'sda',i2c_scl:'scl',i2c_addr:'addr'};"
// cyd_ili9341 ist die feste Werksverdrahtung des ESP32-2432S028 - dort gibt
// es (anders als bei generisch verdrahteten Profilen) nichts anzupassen,
// die Karte "Pin-Belegung" blendet updatePinFields() fuer dieses Profil
// deshalb komplett aus.
"const FIXED_WIRING_KEY='cyd_ili9341';"
"function updatePinFields(){"
"const key=document.getElementById('display_type').value;const d=displays.find(x=>x.key===key);"
"if(!d)return;"
"const fixed=(key===FIXED_WIRING_KEY)||(d.bus==='none');"
"document.getElementById('pinCard').style.display=fixed?'none':'';"
"document.getElementById('pinGroupSpi').style.display=d.bus==='spi'?'':'none';"
"document.getElementById('pinGroupTouch').style.display=d.has_touch?'':'none';"
"document.getElementById('pinGroupI2c').style.display=d.bus==='i2c'?'':'none';"
"document.getElementById('pinGroupNav').style.display=d.shape==='round'?'':'none';"
"PIN_FIELDS.forEach(f=>{const el=document.getElementById('pin_'+f);if(!el)return;"
"const srcKey=PIN_KEY_MAP[f]||f;"
"el.placeholder=(d.pins[srcKey]!==undefined)?('Default: '+d.pins[srcKey]):'Default';"
// Beim Wechsel auf einen ANDEREN Displaytyp als den aktuell aktiven gibt es
// keine passenden Overrides zu zeigen (es existiert nur EIN Override-Set,
// siehe board_profiles.h) - Felder leeren, sonst wuerden die Zahlen des
// aktiven Profils unter falschen Feldbeschriftungen auftauchen.
"if(key!==activeKey)el.value='';"
"});}"
// rect-Profile sind Landscape-verdrahtet (h_res > v_res) - rotation 0/2
// schalten dort intern auf swap_xy=false und damit auf Portrait um, was das
// fest auf Landscape gezeichnete Kachel-UI zerreisst (siehe display_ui.c:
// lcd_init_color_spi()). Nur 1/3 (jeweils Landscape, 180 Grad zueinander)
// ergeben dort ueberhaupt ein brauchbares Bild - andere Shapes (rund/mono)
// behalten alle vier Werte.
"function renderRotationOptions(){"
"const key=document.getElementById('display_type').value;const d=displays.find(x=>x.key===key);"
"const sel=document.getElementById('rotation');const prev=sel.value;"
"const opts=(d&&d.shape==='rect')"
"?[[1,'Standard'],[3,t('rotated_180')+(d.has_touch?t('touch_also_rotated'):'')]]"
":[[0,'Rotation 0'],[1,'Rotation 1'],[2,'Rotation 2'],[3,'Rotation 3']];"
"sel.innerHTML='';opts.forEach(o=>{const el=document.createElement('option');el.value=o[0];el.textContent=o[1];sel.appendChild(el);});"
"if(opts.some(o=>String(o[0])===prev))sel.value=prev;}"
"async function loadDisplays(){const r=await fetch('/api/displays');displays=await r.json();"
"const sel=document.getElementById('display_type');sel.innerHTML='';"
"displays.forEach(d=>{const o=document.createElement('option');o.value=d.key;o.textContent=d.name;sel.appendChild(o);});"
"sel.addEventListener('change',()=>{renderDisplayInfo();renderRotationOptions();updatePinFields();});"
"if(displays.length<=1){"
"document.getElementById('displayChoice').style.display='none';"
"const f=document.getElementById('displayFixed');f.style.display='block';"
"f.textContent=displays.length?(t('fixed_wiring')+': '+displays[0].name):t('no_display_profile');"
"if(displays.length)sel.value=displays[0].key;"
"}}"
"async function loadCfg(){const r=await fetch('/api/config');const c=await r.json();"
// display_type und die davon abgeleiteten Rotation-Optionen muessen VOR dem
// generischen Zuweisungs-Loop unten stehen, sonst waere die Rotation-Auswahl
// beim Laden noch leer (das <select> haette noch keine <option>-Kinder) und
// c.rotation liesse sich nicht setzen.
"if(c.display_type!==undefined)document.getElementById('display_type').value=c.display_type;"
"activeKey=document.getElementById('display_type').value;"
"renderRotationOptions();"
"for(const k in c){const el=document.getElementById(k);if(!el)continue;if(el.type==='checkbox')el.checked=!!c[k];else el.value=(c[k]===null?'':c[k]);}"
"renderDisplayInfo();updatePinFields();}"
"async function loadStatus(){try{const r=await fetch('/api/status');const s=await r.json();"
"document.getElementById('fwVersion').innerText=s.fw_version+t('free_memory_suffix')+Math.round(s.free_heap/1024)+' KB)';"
"document.getElementById('livebar').innerHTML='<span><span class=\"dot\" style=\"background:'+(s.wifi?'#3fd0e0':'#e05a5a')+'\"></span>'+t('card_wifi')+'</span>'+"
"'<span><span class=\"dot\" style=\"background:'+(s.mqtt?'#3fd0e0':'#e05a5a')+'\"></span>MQTT</span>'+"
"'<span>CPU '+s.cpu_load.toFixed(0)+'%</span><span>GPU '+s.gpu_load.toFixed(0)+'%</span><span>IP '+s.ip+'</span>';}catch(e){}}"
"document.getElementById('wifiForm').addEventListener('submit',async(e)=>{e.preventDefault();"
"const payload={wifi_ssid:document.getElementById('wifi_ssid').value,wifi_pass:document.getElementById('wifi_pass').value};"
"document.getElementById('wifiStatus').innerText=t('saving');"
"await fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(payload)});"
"document.getElementById('wifiStatus').innerText=t('saved_restarting');});"
"function toggleAdvanced(){const w=document.getElementById('advancedWrap');const b=document.getElementById('advToggle');"
"const show=w.style.display==='none';w.style.display=show?'block':'none';"
"b.innerText=show?t('hide_advanced'):t('show_advanced');}"
"async function scanWifi(){const s=document.getElementById('wifiScanStatus');const box=document.getElementById('wifiList');"
"s.innerText=t('searching');box.style.display='none';box.innerHTML='';"
"try{const r=await fetch('/api/wifi_scan');const nets=await r.json();"
// Nach SSID dedupliziert (staerkstes Signal gewinnt), absteigend nach RSSI
// sortiert. Als Buttons statt <datalist> aufgebaut - <datalist> zeigte auf
// mobilen Browsern (Android Chrome/iOS Safari) keine oder nur unzuverlaessige
// Vorschlaege. textContent statt innerHTML, da SSIDs nicht vertrauenswuerdig
// sind (koennten HTML-Sonderzeichen enthalten).
"const best={};nets.forEach(n=>{if(!(n.ssid in best)||n.rssi>best[n.ssid].rssi)best[n.ssid]=n;});"
"const list=Object.values(best).sort((a,b)=>b.rssi-a.rssi);"
"list.forEach(n=>{const b=document.createElement('button');b.type='button';"
"const nm=document.createElement('span');nm.textContent=n.ssid;"
"const rs=document.createElement('span');rs.className='rssi';rs.textContent=n.rssi+'dBm';"
"b.appendChild(nm);b.appendChild(rs);"
"b.onclick=()=>{document.getElementById('wifi_ssid').value=n.ssid;box.style.display='none';};"
"box.appendChild(b);});"
"box.style.display=list.length?'block':'none';"
"s.innerText=list.length?list.length+' '+t('networks_found'):t('no_networks_found');"
"}catch(e){s.innerText=t('search_error');}}"
"document.getElementById('cfgForm').addEventListener('submit',async(e)=>{e.preventDefault();"
"const ids=['mqtt_host','mqtt_port','mqtt_user','mqtt_pass','mqtt_topic','ntp_server','tz','weather_enabled','weather_api_key','weather_city','weather_units','brightness','rotation','language','display_type','standby_timeout_s','color_invert'];"
"PIN_FIELDS.forEach(f=>ids.push('pin_'+f));"
"const payload={};ids.forEach(id=>{const el=document.getElementById(id);if(!el)return;"
"if(el.type==='checkbox')payload[id]=el.checked;"
// Leeres Zahlenfeld -> null (= "kein Override, Default verwenden" auf dem
// Geraet), nicht 0 - 0 waere ein gueltiger, aber i.d.R. falscher GPIO-Wert.
"else if(el.type==='number')payload[id]=(el.value===''?null:Number(el.value));"
// rotation ist ein <select> (id==='number' greift hier nicht) - der Server
// erwartet trotzdem eine JSON-Zahl, nicht den String, den el.value liefert.
"else if(id==='rotation')payload[id]=Number(el.value);"
"else payload[id]=el.value;});"
"document.getElementById('status').innerText=t('saving');"
"await fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(payload)});"
"document.getElementById('status').innerText=t('saved_restarting');});"
"function uploadFirmware(){const f=document.getElementById('fwFile').files[0];"
"if(!f){alert(t('select_bin_first'));return;}"
"if(!confirm(t('confirm_flash_prefix')+f.name+t('confirm_flash_suffix')))return;"
"const p=document.getElementById('otaProgress'),s=document.getElementById('otaStatus');"
"p.style.display='block';p.value=0;s.innerText=t('uploading');"
"const xhr=new XMLHttpRequest();xhr.open('POST','/update',true);"
"xhr.setRequestHeader('Content-Type','application/octet-stream');"
"xhr.upload.onprogress=function(e){if(e.lengthComputable){const pct=Math.round(e.loaded/e.total*100);p.value=pct;s.innerText=t('uploading')+' '+pct+'%';}};"
"xhr.onload=function(){s.innerText=xhr.status===200?t('update_success'):t('update_fail_prefix')+xhr.responseText;};"
"xhr.onerror=function(){s.innerText=t('upload_conn_error');};xhr.send(f);}"
// Sprachauswahl: gespeicherte Praeferenz > Browser-Sprache > Fallback 'de'.
// Rein client-seitig, unabhaengig vom Geraete-Display-Sprachfeld ("language"
// im Formular oben), das erst nach einem Neustart wirkt.
"let initialLang=localStorage.getItem('lang')||(navigator.language||'de').slice(0,2);"
"if(!I18N[initialLang])initialLang='de';"
"applyI18n(initialLang);"
"document.getElementById('langSwitch').addEventListener('change',(e)=>{localStorage.setItem('lang',e.target.value);applyI18n(e.target.value);});"
"loadDisplays().then(loadCfg);loadStatus();setInterval(loadStatus,2000);"
"</script></body></html>";

// ------------------------------------------------------------------
// Neustart nach kurzer Verzoegerung (damit die HTTP-Antwort noch rausgeht)
// ------------------------------------------------------------------
static void restart_cb(void *arg) { esp_restart(); }

static void schedule_restart(int ms)
{
    const esp_timer_create_args_t a = { .callback = restart_cb, .name = "restart" };
    esp_timer_handle_t t;
    if (esp_timer_create(&a, &t) == ESP_OK) esp_timer_start_once(t, (uint64_t)ms * 1000);
}

// ------------------------------------------------------------------
// WLAN-Events
// ------------------------------------------------------------------
static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_connected = false;
        if (s_retry < STA_MAX_RETRY) {
            s_retry++;
            esp_wifi_connect();
        } else {
            xEventGroupSetBits(s_wifi_events, WIFI_FAIL_BIT);
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *evt = (ip_event_got_ip_t *)data;
        snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&evt->ip_info.ip));
        wifi_connected = true;
        s_retry = 0;
        xEventGroupSetBits(s_wifi_events, WIFI_CONNECTED_BIT);
    }
}

// Einmalige WLAN-Grundinitialisierung: beide Default-Netifs anlegen, Treiber
// initialisieren, Event-Handler registrieren.
static void wifi_common_init(void)
{
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t ic = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&ic));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                        wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                        wifi_event_handler, NULL, NULL));
}

static bool wifi_connect_sta(void)
{
    if (strlen(app_config.wifi_ssid) == 0) return false;

    wifi_config_t wc = { 0 };
    strlcpy((char *)wc.sta.ssid,     app_config.wifi_ssid, sizeof(wc.sta.ssid));
    strlcpy((char *)wc.sta.password, app_config.wifi_pass, sizeof(wc.sta.password));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());

    EventBits_t bits = xEventGroupWaitBits(s_wifi_events, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE, pdFALSE, pdMS_TO_TICKS(STA_TIMEOUT_MS));
    return (bits & WIFI_CONNECTED_BIT) != 0;
}

// ------------------------------------------------------------------
// Captive-DNS: beantwortet jede A-Anfrage mit der AP-IP (192.168.4.1)
// ------------------------------------------------------------------
static void dns_server_task(void *arg)
{
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) { vTaskDelete(NULL); return; }
    struct sockaddr_in server = {
        .sin_family = AF_INET, .sin_port = htons(53), .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (bind(sock, (struct sockaddr *)&server, sizeof(server)) < 0) {
        close(sock); vTaskDelete(NULL); return;
    }

    uint8_t buf[512];
    for (;;) {
        struct sockaddr_in client;
        socklen_t clen = sizeof(client);
        int len = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr *)&client, &clen);
        if (len < (int)sizeof(uint16_t) * 6) continue;

        // Antwort-Header: Standard-Query-Antwort, 1 Question, 1 Answer.
        buf[2] |= 0x80;   // QR = Antwort
        buf[3] |= 0x80;   // RA
        buf[7] = 1;       // ANCOUNT = 1 (Answer)

        int qlen = len; // Frage 1:1 anhaengen; Antwort dahinter
        if (qlen + 16 > (int)sizeof(buf)) continue;
        uint8_t *p = buf + qlen;
        *p++ = 0xC0; *p++ = 0x0C;             // Name-Pointer auf Frage
        *p++ = 0x00; *p++ = 0x01;             // TYPE A
        *p++ = 0x00; *p++ = 0x01;             // CLASS IN
        *p++ = 0x00; *p++ = 0x00; *p++ = 0x00; *p++ = 0x3C; // TTL 60s
        *p++ = 0x00; *p++ = 0x04;             // RDLENGTH 4
        *p++ = 192; *p++ = 168; *p++ = 4; *p++ = 1; // 192.168.4.1

        sendto(sock, buf, qlen + 16, 0, (struct sockaddr *)&client, clen);
    }
}

static void start_ap(void)
{
    s_ap_mode = true;
    esp_wifi_stop(); // evtl. laufenden STA-Versuch beenden (Fehler ignorieren)

    uint8_t mac[6] = { 0 };
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    snprintf(s_ap_ssid, sizeof(s_ap_ssid), "ESP32-HWMon-%02x%02x", mac[4], mac[5]);

    wifi_config_t ap = { 0 };
    strlcpy((char *)ap.ap.ssid, s_ap_ssid, sizeof(ap.ap.ssid));
    ap.ap.ssid_len       = strlen(s_ap_ssid);
    ap.ap.channel        = 1;
    ap.ap.max_connection = 4;
    ap.ap.authmode       = WIFI_AUTH_OPEN;   // offen: vermeidet WPA2/WPA3-Handshake-Probleme

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
    ESP_ERROR_CHECK(esp_wifi_start());

    strcpy(s_ip, "192.168.4.1");
    xTaskCreate(dns_server_task, "dns", 3072, NULL, 4, NULL);
    ESP_LOGI(TAG, "Setup-AP aktiv: SSID '%s' (offen), IP 192.168.4.1", s_ap_ssid);
}

// ------------------------------------------------------------------
// HTTP-Handler
// ------------------------------------------------------------------
static esp_err_t h_root(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, INDEX_HTML, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t h_status(httpd_req_t *req)
{
    char buf[320];
    int n = snprintf(buf, sizeof(buf),
        "{\"wifi\":%s,\"mqtt\":%s,\"ip\":\"%s\",\"cpu_load\":%.1f,\"gpu_load\":%.1f,"
        "\"cpu_temp\":%.1f,\"gpu_temp\":%.1f,\"cpu_power\":%.1f,\"gpu_power\":%.1f,"
        "\"fw_version\":\"%s\",\"free_heap\":%u}",
        wifi_connected ? "true" : "false", mqtt_connected ? "true" : "false", s_ip,
        hw_info.cpu_load, hw_info.gpu_load, hw_info.cpu_temp, hw_info.gpu_temp,
        hw_info.cpu_power, hw_info.gpu_power, FW_VERSION,
        (unsigned)esp_get_free_heap_size());
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, buf, n);
}

// Scannt nach WLAN-Netzwerken, waehrend der Setup-AP (falls aktiv) weiter
// laeuft - dafuer kurzzeitig auf APSTA umschalten statt reinem AP-Modus,
// sonst wuerde das Handy vom Setup-AP getrennt. Nach dem Scan wieder auf den
// vorherigen Modus zurueckschalten.
static esp_err_t h_wifi_scan(httpd_req_t *req)
{
    wifi_mode_t prev_mode = WIFI_MODE_NULL;
    esp_wifi_get_mode(&prev_mode);
    if (s_ap_mode && prev_mode == WIFI_MODE_AP) {
        esp_wifi_set_mode(WIFI_MODE_APSTA);
    }

    wifi_scan_config_t scan_cfg = { 0 };
    esp_err_t err = esp_wifi_scan_start(&scan_cfg, true);

    cJSON *arr = cJSON_CreateArray();
    if (err == ESP_OK) {
        uint16_t num = 0;
        esp_wifi_scan_get_ap_num(&num);
        if (num > 20) num = 20;
        wifi_ap_record_t *aps = calloc(num, sizeof(wifi_ap_record_t));
        if (aps) {
            esp_wifi_scan_get_ap_records(&num, aps);
            for (int i = 0; i < num; i++) {
                if (aps[i].ssid[0] == '\0') continue;
                cJSON *o = cJSON_CreateObject();
                cJSON_AddStringToObject(o, "ssid", (const char *)aps[i].ssid);
                cJSON_AddNumberToObject(o, "rssi", aps[i].rssi);
                cJSON_AddItemToArray(arr, o);
            }
            free(aps);
        }
    } else {
        ESP_LOGW(TAG, "WLAN-Scan fehlgeschlagen: %s", esp_err_to_name(err));
    }

    if (s_ap_mode && prev_mode == WIFI_MODE_AP) {
        esp_wifi_set_mode(WIFI_MODE_AP);
    }

    char *json = cJSON_PrintUnformatted(arr);
    httpd_resp_set_type(req, "application/json");
    esp_err_t send_err = httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
    free(json);
    cJSON_Delete(arr);
    return send_err;
}

// cJSON hat kein eingebautes "Zahl oder null" - Pin-Overrides sind entweder
// ein konkreter GPIO-Wert oder PIN_UNSET/0 ("kein Override, siehe
// board_profiles.h"), letzteres muss das Webportal als leeres Formularfeld
// darstellen koennen (0 waere ein gueltiger, aber i.d.R. falscher GPIO-Wert).
static void add_pin_or_null(cJSON *d, const char *key, int16_t v)
{
    if (v == PIN_UNSET) cJSON_AddNullToObject(d, key);
    else cJSON_AddNumberToObject(d, key, v);
}

static esp_err_t h_config_get(httpd_req_t *req)
{
    cJSON *d = cJSON_CreateObject();
    cJSON_AddStringToObject(d, "wifi_ssid", app_config.wifi_ssid);
    cJSON_AddStringToObject(d, "wifi_pass", "");        // Passwoerter nie zuruecksenden
    cJSON_AddStringToObject(d, "mqtt_host", app_config.mqtt_host);
    cJSON_AddNumberToObject(d, "mqtt_port", app_config.mqtt_port);
    cJSON_AddStringToObject(d, "mqtt_user", app_config.mqtt_user);
    cJSON_AddStringToObject(d, "mqtt_pass", "");
    cJSON_AddStringToObject(d, "mqtt_topic", app_config.mqtt_topic);
    cJSON_AddStringToObject(d, "ntp_server", app_config.ntp_server);
    cJSON_AddStringToObject(d, "tz", app_config.tz);
    cJSON_AddBoolToObject(  d, "weather_enabled", app_config.weather_enabled);
    cJSON_AddStringToObject(d, "weather_api_key", "");
    cJSON_AddStringToObject(d, "weather_city", app_config.weather_city);
    cJSON_AddStringToObject(d, "weather_units", app_config.weather_units);
    cJSON_AddNumberToObject(d, "brightness", app_config.brightness);
    cJSON_AddNumberToObject(d, "rotation", app_config.rotation);
    cJSON_AddStringToObject(d, "display_type", board_profile_key(app_config.display_type));
    cJSON_AddNumberToObject(d, "standby_timeout_s", app_config.standby_timeout_s);
    cJSON_AddBoolToObject(  d, "color_invert", app_config.color_invert);
    cJSON_AddStringToObject(d, "language", app_config.language);

    const pin_override_t *ov = &app_config.pin_overrides;
    add_pin_or_null(d, "pin_mosi", ov->mosi);
    add_pin_or_null(d, "pin_miso", ov->miso);
    add_pin_or_null(d, "pin_sclk", ov->sclk);
    add_pin_or_null(d, "pin_cs",   ov->cs);
    add_pin_or_null(d, "pin_dc",   ov->dc);
    add_pin_or_null(d, "pin_rst",  ov->rst);
    add_pin_or_null(d, "pin_bl",   ov->bl);
    add_pin_or_null(d, "pin_touch_cs",   ov->touch_cs);
    add_pin_or_null(d, "pin_touch_irq",  ov->touch_irq);
    add_pin_or_null(d, "pin_touch_mosi", ov->touch_mosi);
    add_pin_or_null(d, "pin_touch_miso", ov->touch_miso);
    add_pin_or_null(d, "pin_touch_clk",  ov->touch_clk);
    add_pin_or_null(d, "pin_i2c_sda", ov->i2c_sda);
    add_pin_or_null(d, "pin_i2c_scl", ov->i2c_scl);
    if (ov->i2c_addr == 0) cJSON_AddNullToObject(d, "pin_i2c_addr");
    else cJSON_AddNumberToObject(d, "pin_i2c_addr", ov->i2c_addr);
    add_pin_or_null(d, "pin_nav_button", ov->nav_button);

    char *out = cJSON_PrintUnformatted(d);
    httpd_resp_set_type(req, "application/json");
    esp_err_t r = httpd_resp_send(req, out, HTTPD_RESP_USE_STRLEN);
    cJSON_free(out);
    cJSON_Delete(d);
    return r;
}

// Liefert alle unterstuetzten Displaytypen samt Pinbelegung als JSON-Array,
// damit das Webportal Dropdown + Verdrahtungstabelle rendern kann, ohne die
// board_profiles.c-Tabelle im JS zu duplizieren.
static esp_err_t h_displays(httpd_req_t *req)
{
    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < DISPLAY_TYPE_COUNT; i++) {
        // Nur Profile listen, die auf diesem Compile-Target (idf.py
        // set-target) ueberhaupt Sinn ergeben - auf klassischem ESP32 nur
        // CYD, auf allen anderen Chips nur die generischen Profile. Damit
        // gibt es auf dem CYD-Build gar keine Auswahl (nur 1 Eintrag), das
        // Webportal blendet das Dropdown dann aus (siehe renderDisplayInfo).
        if (!board_profile_is_available((display_type_t)i)) continue;
        const board_profile_t *p = board_profile_get((display_type_t)i);
        cJSON *d = cJSON_CreateObject();
        cJSON_AddStringToObject(d, "key", board_profile_key((display_type_t)i));
        cJSON_AddStringToObject(d, "name", p->name);
        cJSON_AddStringToObject(d, "bus", i == DISPLAY_NONE ? "none" : (p->bus == LCD_BUS_I2C ? "i2c" : "spi"));
        cJSON_AddStringToObject(d, "shape",
            p->shape == LCD_SHAPE_ROUND ? "round" : (p->shape == LCD_SHAPE_MONO ? "mono" : "rect"));
        cJSON_AddBoolToObject(d, "has_touch", p->has_touch);
        cJSON_AddNumberToObject(d, "h_res", p->h_res);
        cJSON_AddNumberToObject(d, "v_res", p->v_res);

        cJSON *pins = cJSON_CreateObject();
        if (i == DISPLAY_NONE) {
            // keine Pins - JS blendet Pin-Karte/Verdrahtungstabelle fuer
            // bus==='none' komplett aus.
        } else if (p->bus == LCD_BUS_I2C) {
            cJSON_AddNumberToObject(pins, "sda", p->i2c_sda);
            cJSON_AddNumberToObject(pins, "scl", p->i2c_scl);
            cJSON_AddNumberToObject(pins, "addr", p->i2c_addr);
        } else {
            cJSON_AddNumberToObject(pins, "mosi", p->mosi);
            cJSON_AddNumberToObject(pins, "miso", p->miso);
            cJSON_AddNumberToObject(pins, "sclk", p->sclk);
            cJSON_AddNumberToObject(pins, "cs",   p->cs);
            cJSON_AddNumberToObject(pins, "dc",   p->dc);
            cJSON_AddNumberToObject(pins, "rst",  p->rst);
            cJSON_AddNumberToObject(pins, "bl",   p->bl);
            if (p->has_touch) {
                cJSON_AddNumberToObject(pins, "touch_cs",   p->touch_cs);
                cJSON_AddNumberToObject(pins, "touch_irq",  p->touch_irq);
                cJSON_AddNumberToObject(pins, "touch_mosi", p->touch_mosi);
                cJSON_AddNumberToObject(pins, "touch_miso", p->touch_miso);
                cJSON_AddNumberToObject(pins, "touch_clk",  p->touch_clk);
            }
        }
        cJSON_AddNumberToObject(pins, "nav_button", p->nav_button);
        cJSON_AddItemToObject(d, "pins", pins);
        cJSON_AddItemToArray(arr, d);
    }
    char *out = cJSON_PrintUnformatted(arr);
    httpd_resp_set_type(req, "application/json");
    esp_err_t r = httpd_resp_send(req, out, HTTPD_RESP_USE_STRLEN);
    cJSON_free(out);
    cJSON_Delete(arr);
    return r;
}

static void cfg_str(cJSON *root, const char *key, char *dst, size_t sz, bool skip_empty)
{
    cJSON *v = cJSON_GetObjectItem(root, key);
    if (cJSON_IsString(v)) {
        if (skip_empty && strlen(v->valuestring) == 0) return;
        strlcpy(dst, v->valuestring, sz);
    }
}

// Pin-Override-Feld setzen: JSON null -> PIN_UNSET ("kein Override, Default
// verwenden"), Zahl -> konkreter GPIO-Override. Fehlt das Feld im Request
// komplett, bleibt der aktuelle Wert unveraendert (das Webportal sendet aber
// immer alle Pin-Felder mit, siehe PIN_FIELDS im JS).
static void cfg_pin(cJSON *root, const char *key, int16_t *dst)
{
    cJSON *v = cJSON_GetObjectItem(root, key);
    if (!v) return;
    if (cJSON_IsNull(v)) *dst = PIN_UNSET;
    else if (cJSON_IsNumber(v)) *dst = (int16_t)v->valuedouble;
}

static void cfg_pin_addr(cJSON *root, const char *key, uint8_t *dst)
{
    cJSON *v = cJSON_GetObjectItem(root, key);
    if (!v) return;
    if (cJSON_IsNull(v)) *dst = 0;
    else if (cJSON_IsNumber(v)) *dst = (uint8_t)v->valuedouble;
}

static esp_err_t h_config_post(httpd_req_t *req)
{
    int total = req->content_len;
    if (total <= 0 || total > 4096) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad length");
        return ESP_FAIL;
    }
    char *body = malloc(total + 1);
    if (!body) { httpd_resp_send_500(req); return ESP_FAIL; }

    int received = 0;
    while (received < total) {
        int r = httpd_req_recv(req, body + received, total - received);
        if (r <= 0) { free(body); httpd_resp_send_500(req); return ESP_FAIL; }
        received += r;
    }
    body[total] = '\0';

    cJSON *root = cJSON_Parse(body);
    free(body);
    if (!root) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"error\":\"invalid json\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    cfg_str(root, "wifi_ssid",  app_config.wifi_ssid,  sizeof(app_config.wifi_ssid),  false);
    cfg_str(root, "wifi_pass",  app_config.wifi_pass,  sizeof(app_config.wifi_pass),  true);
    cfg_str(root, "mqtt_host",  app_config.mqtt_host,  sizeof(app_config.mqtt_host),  false);
    cJSON *port = cJSON_GetObjectItem(root, "mqtt_port");
    if (cJSON_IsNumber(port)) app_config.mqtt_port = (uint16_t)port->valuedouble;
    cfg_str(root, "mqtt_user",  app_config.mqtt_user,  sizeof(app_config.mqtt_user),  false);
    cfg_str(root, "mqtt_pass",  app_config.mqtt_pass,  sizeof(app_config.mqtt_pass),  true);
    cfg_str(root, "mqtt_topic", app_config.mqtt_topic, sizeof(app_config.mqtt_topic), false);
    cfg_str(root, "ntp_server", app_config.ntp_server, sizeof(app_config.ntp_server), false);
    cfg_str(root, "tz",         app_config.tz,         sizeof(app_config.tz),         false);
    cJSON *wen = cJSON_GetObjectItem(root, "weather_enabled");
    if (cJSON_IsBool(wen)) app_config.weather_enabled = cJSON_IsTrue(wen);
    cfg_str(root, "weather_api_key", app_config.weather_api_key, sizeof(app_config.weather_api_key), true);
    cfg_str(root, "weather_city",    app_config.weather_city,    sizeof(app_config.weather_city),    false);
    cfg_str(root, "weather_units",   app_config.weather_units,   sizeof(app_config.weather_units),   false);
    cJSON *br = cJSON_GetObjectItem(root, "brightness");
    if (cJSON_IsNumber(br)) app_config.brightness = (uint8_t)br->valuedouble;
    cJSON *ro = cJSON_GetObjectItem(root, "rotation");
    if (cJSON_IsNumber(ro)) app_config.rotation = (uint8_t)ro->valuedouble;
    cJSON *dt = cJSON_GetObjectItem(root, "display_type");
    if (cJSON_IsString(dt)) {
        display_type_t new_type = board_profile_from_key(dt->valuestring);
        if (new_type != app_config.display_type) {
            // Displaytyp gewechselt: alte Pin-Overrides galten fuer ein
            // anderes Panel/Bus und passen hier i.d.R. nicht mehr - reset,
            // bevor unten eventuell neue Overrides fuer das neue Profil
            // aus demselben Request angewendet werden.
            pin_override_set_defaults(&app_config.pin_overrides);
        }
        app_config.display_type = new_type;
    }
    cJSON *sb = cJSON_GetObjectItem(root, "standby_timeout_s");
    if (cJSON_IsNumber(sb)) app_config.standby_timeout_s = (uint16_t)sb->valuedouble;
    cJSON *inv = cJSON_GetObjectItem(root, "color_invert");
    if (cJSON_IsBool(inv)) app_config.color_invert = cJSON_IsTrue(inv);
    cfg_str(root, "language", app_config.language, sizeof(app_config.language), false);

    pin_override_t *ov = &app_config.pin_overrides;
    cfg_pin(root, "pin_mosi", &ov->mosi);
    cfg_pin(root, "pin_miso", &ov->miso);
    cfg_pin(root, "pin_sclk", &ov->sclk);
    cfg_pin(root, "pin_cs",   &ov->cs);
    cfg_pin(root, "pin_dc",   &ov->dc);
    cfg_pin(root, "pin_rst",  &ov->rst);
    cfg_pin(root, "pin_bl",   &ov->bl);
    cfg_pin(root, "pin_touch_cs",   &ov->touch_cs);
    cfg_pin(root, "pin_touch_irq",  &ov->touch_irq);
    cfg_pin(root, "pin_touch_mosi", &ov->touch_mosi);
    cfg_pin(root, "pin_touch_miso", &ov->touch_miso);
    cfg_pin(root, "pin_touch_clk",  &ov->touch_clk);
    cfg_pin(root, "pin_i2c_sda", &ov->i2c_sda);
    cfg_pin(root, "pin_i2c_scl", &ov->i2c_scl);
    cfg_pin_addr(root, "pin_i2c_addr", &ov->i2c_addr);
    cfg_pin(root, "pin_nav_button", &ov->nav_button);

    cJSON_Delete(root);

    config_store_save(&app_config);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"ok\":true}", HTTPD_RESP_USE_STRLEN);
    schedule_restart(1500);
    return ESP_OK;
}

// Raw-Binary-OTA: der Request-Body IST die .bin.
static esp_err_t h_update(httpd_req_t *req)
{
    const esp_partition_t *part = esp_ota_get_next_update_partition(NULL);
    if (!part) { httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no ota part"); return ESP_FAIL; }

    // Tatsaechliche Groesse statt OTA_SIZE_UNKNOWN uebergeben: sonst loescht
    // esp_ota_begin() sofort die komplette (1.75 MB grosse) Zielpartition auf
    // einmal, was den Flash-Cache fuer mehrere Sekunden blockiert. In dieser
    // Zeit kann der WLAN-/LWIP-Stack keine Pakete mehr bedienen und der
    // Browser bricht den Upload mit "Verbindungsfehler" ab. Mit bekannter
    // Groesse werden nur die tatsaechlich benoetigten Sektoren geloescht.
    esp_ota_handle_t ota;
    size_t ota_size = req->content_len > 0 ? (size_t)req->content_len : OTA_SIZE_UNKNOWN;
    if (esp_ota_begin(part, ota_size, &ota) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "ota begin"); return ESP_FAIL;
    }

    char buf[1024];
    int remaining = req->content_len;
    while (remaining > 0) {
        int r = httpd_req_recv(req, buf, remaining < (int)sizeof(buf) ? remaining : (int)sizeof(buf));
        if (r <= 0) {
            if (r == HTTPD_SOCK_ERR_TIMEOUT) continue;
            esp_ota_abort(ota);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "recv");
            return ESP_FAIL;
        }
        if (esp_ota_write(ota, buf, r) != ESP_OK) {
            esp_ota_abort(ota);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "ota write");
            return ESP_FAIL;
        }
        remaining -= r;
    }

    if (esp_ota_end(ota) != ESP_OK || esp_ota_set_boot_partition(part) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "ota finalize");
        return ESP_FAIL;
    }

    httpd_resp_sendstr(req, "OK");
    ESP_LOGI(TAG, "OTA erfolgreich, Neustart folgt");
    schedule_restart(1500);
    return ESP_OK;
}

// Captive-Portal: alle unbekannten Pfade auf die Startseite umleiten (nur AP).
static esp_err_t h_captive_redirect(httpd_req_t *req)
{
    if (s_ap_mode) {
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
        httpd_resp_send(req, NULL, 0);
        return ESP_OK;
    }
    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not found");
    return ESP_FAIL;
}

static esp_err_t h_404(httpd_req_t *req, httpd_err_code_t err)
{
    return h_captive_redirect(req);
}

static void start_http(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.stack_size      = 8192;
    cfg.max_uri_handlers = 16;
    cfg.lru_purge_enable = true;
    // Handys pruefen die Internetverbindung ueber mehrere parallele Anfragen
    // (iOS/macOS: hotspot-detect.html, Android: generate_204, Windows:
    // connecttest.txt, ...). Mit dem esp_http_server-Default (abgeleitet aus
    // CONFIG_LWIP_MAX_SOCKETS, siehe sdkconfig.defaults) gingen dabei die
    // Sockets aus ("error in accept (23)"/"error in recv: 104" im Log) - das
    // Config-Formular blieb dann haengen bzw. das Handy hat die Captive-
    // Portal-Seite neu geladen (dadurch sprang der Fokus zurueck aufs erste
    // Feld). Zusammen mit CONFIG_LWIP_MAX_SOCKETS=16 jetzt mehr Luft.
    cfg.max_open_sockets = 13;
    cfg.recv_wait_timeout = 3;
    cfg.send_wait_timeout = 3;
    if (httpd_start(&s_httpd, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start fehlgeschlagen");
        return;
    }

    httpd_uri_t routes[] = {
        { .uri = "/",             .method = HTTP_GET,  .handler = h_root },
        { .uri = "/api/status",   .method = HTTP_GET,  .handler = h_status },
        { .uri = "/api/config",   .method = HTTP_GET,  .handler = h_config_get },
        { .uri = "/api/config",   .method = HTTP_POST, .handler = h_config_post },
        { .uri = "/api/displays", .method = HTTP_GET,  .handler = h_displays },
        { .uri = "/api/wifi_scan", .method = HTTP_GET, .handler = h_wifi_scan },
        { .uri = "/update",       .method = HTTP_POST, .handler = h_update },
        // Bekannte Captive-Portal-Erkennungspfade der wichtigsten Betriebs-
        // systeme direkt registrieren (statt nur ueber den generischen
        // 404-Handler laufen zu lassen) - vermeidet die "URI not found"-
        // Warnung im Log und beantwortet die Anfrage etwas schneller/mit
        // weniger Overhead pro Verbindung.
        { .uri = "/hotspot-detect.html",     .method = HTTP_GET, .handler = h_captive_redirect }, // iOS/macOS
        { .uri = "/library/test/success.html", .method = HTTP_GET, .handler = h_captive_redirect }, // iOS/macOS (alt)
        { .uri = "/generate_204",            .method = HTTP_GET, .handler = h_captive_redirect }, // Android
        { .uri = "/gen_204",                 .method = HTTP_GET, .handler = h_captive_redirect }, // Android (alt)
        { .uri = "/connecttest.txt",         .method = HTTP_GET, .handler = h_captive_redirect }, // Windows
        { .uri = "/ncsi.txt",                .method = HTTP_GET, .handler = h_captive_redirect }, // Windows (alt)
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        httpd_register_uri_handler(s_httpd, &routes[i]);
    }
    httpd_register_err_handler(s_httpd, HTTPD_404_NOT_FOUND, h_404);
}

// ------------------------------------------------------------------
void web_portal_begin(void)
{
    s_wifi_events = xEventGroupCreate();
    wifi_common_init();

    if (wifi_connect_sta()) {
        wifi_connected = true;
        ESP_LOGI(TAG, "WLAN verbunden, IP %s", s_ip);
    } else {
        wifi_connected = false;
        start_ap();
    }

    start_http();
}

bool        web_portal_ap_mode(void)  { return s_ap_mode; }
const char *web_portal_ip(void)       { return s_ip; }
const char *web_portal_ap_ssid(void)  { return s_ap_ssid; }

void web_portal_force_ap(void)
{
    wifi_connected = false;
    mqtt_connected  = false;
    start_ap();
}
