#include "ui_home.h"
#include "ui_time.h"
#include "ui_settings.h"
#include "ui_forecast.h"
#include "ui_font_pl.h"
#include "ui_i18n.h"
#include "ui.h"                 /* daje LV_IMG_DECLARE(...) dla ikon oraz ui_screen_setting */
#include "view_data.h"
#include "indicator_weather.h"

#include "esp_event.h"
#include "esp_log.h"
#include "lv_port.h"
#include <time.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

static const char *TAG = "ui_home";

/* Ikony zasiegu WiFi nie sa deklarowane w ui.h (jest tam tylko wifi_disconet),
 * a ich definicje siedza w ui_img_wifi_1/2/3_png.c -> deklarujemy je tutaj. */
LV_IMG_DECLARE(ui_img_wifi_1_png);
LV_IMG_DECLARE(ui_img_wifi_2_png);
LV_IMG_DECLARE(ui_img_wifi_3_png);

lv_obj_t *ui_home = NULL;

/* domyslne wartosci; zostana nadpisane przez model przy starcie */
struct view_data_display  g_disp_cfg  = { .brightness = 80, .sleep_mode_en = false, .sleep_mode_time_min = 5 };
struct view_data_time_cfg g_time_cfg  = { .time_format_24 = true, .auto_update = true, .auto_update_zone = true, .daylight = true, .zone = 0 };

/* --- uchwyty do etykiet, ktore aktualizujemy w locie --- */
static lv_obj_t *lbl_time;
static lv_obj_t *lbl_date;
static lv_obj_t *img_wifi;
static lv_obj_t *wx_icon;          /* kolorowa "ikonka" pogody (chip) */
static lv_obj_t *lbl_wx_temp;
static lv_obj_t *lbl_wx_city;
static lv_obj_t *lbl_wx_desc;

/* dane -> wartosc na kafelku */
static lv_obj_t *lbl_co2, *lbl_tvoc, *lbl_temp, *lbl_hum;

static bool s_time_24h = true;

/* ============================ style / helpery ============================ */

/* Jeden kafelek: panel + ikonka + tytul + duza wartosc + jednostka.
 * Zwraca panel, a przez out_value oddaje etykiete wartosci do aktualizacji. */
/* Klik w kafel -> ekran historii/wykresu (stockowy ui_screen_sensor_chart).
 * Wysylamy zadanie historii dla danego czujnika, model odsyla dane a stockowy
 * handler aktualizuje wykres; my ladujemy ekran wykresu. */
static void tile_cb(lv_event_t *e)
{
    enum sensor_data_type type = (enum sensor_data_type)(intptr_t)lv_event_get_user_data(e);
    int32_t ev = VIEW_EVENT_SENSOR_CO2_HISTORY;
    switch (type) {
    case SENSOR_DATA_CO2:      ev = VIEW_EVENT_SENSOR_CO2_HISTORY;      break;
    case SENSOR_DATA_TVOC:     ev = VIEW_EVENT_SENSOR_TVOC_HISTORY;     break;
    case SENSOR_DATA_TEMP:     ev = VIEW_EVENT_SENSOR_TEMP_HISTORY;     break;
    case SENSOR_DATA_HUMIDITY: ev = VIEW_EVENT_SENSOR_HUMIDITY_HISTORY; break;
    default: break;
    }
    esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, ev, NULL, 0, portMAX_DELAY);
    if (ui_screen_sensor_chart) lv_disp_load_scr(ui_screen_sensor_chart);
}

static lv_obj_t *make_tile(lv_obj_t *parent, const lv_img_dsc_t *icon,
                           const char *title, const char *unit,
                           enum sensor_data_type type, lv_obj_t **out_value)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, 200, 132);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(card, 18, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x1E2530), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_shadow_width(card, 12, 0);
    lv_obj_set_style_shadow_opa(card, LV_OPA_20, 0);
    lv_obj_set_style_pad_all(card, 10, 0);
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(card, tile_cb, LV_EVENT_CLICKED, (void *)(intptr_t)type);

    /* ikonka w lewym gornym rogu */
    lv_obj_t *ic = lv_img_create(card);
    lv_img_set_src(ic, icon);
    lv_obj_align(ic, LV_ALIGN_TOP_LEFT, 0, 0);

    /* tytul obok ikonki */
    lv_obj_t *t = lv_label_create(card);
    lv_label_set_text(t, title);
    lv_obj_set_style_text_color(t, lv_color_hex(0x9AA5B1), 0);
    lv_obj_align(t, LV_ALIGN_TOP_LEFT, 44, 6);

    /* duza wartosc na srodku */
    lv_obj_t *v = lv_label_create(card);
    lv_label_set_text(v, "--");
    lv_obj_set_style_text_color(v, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(v, &lv_font_montserrat_28, 0);
    lv_obj_align(v, LV_ALIGN_BOTTOM_LEFT, 0, -4);

    /* jednostka po prawej na dole */
    lv_obj_t *u = lv_label_create(card);
    lv_label_set_text(u, unit);
    lv_obj_set_style_text_color(u, lv_color_hex(0x6B7684), 0);
    lv_obj_align(u, LV_ALIGN_BOTTOM_RIGHT, 0, -8);

    if (out_value) *out_value = v;
    return card;
}

/* Wybor obrazka WiFi wg RSSI / stanu polaczenia */
static const lv_img_dsc_t *wifi_img_for(bool connected, int8_t rssi)
{
    if (!connected) return &ui_img_wifi_disconet_png;
    if (rssi >= -60) return &ui_img_wifi_3_png;
    if (rssi >= -75) return &ui_img_wifi_2_png;
    return &ui_img_wifi_1_png;
}

/* Klik w trybik -> NASZ ekran ustawien (lokalizacja/pogoda, NTP, WiFi, czas) */
static void gear_cb(lv_event_t *e)
{
    ui_settings_open();
}

/* Klik w pogode (dol ekranu) -> prognoza 3-dniowa */
static void weather_row_cb(lv_event_t *e)
{
    ui_forecast_open();
}

/* "Straznik domu": stockowy firmware po starcie i po roznych zdarzeniach
 * (SCREEN_START, powrot z WiFi, gesty) sam laduje swoje ekrany ui_screen_time /
 * ui_screen_sensor. Ten timer wykrywa taka sytuacje i natychmiast wraca na nasz
 * ekran glowny. Dzieki temu nasz UI jest faktycznym "domem", niezaleznie od tego
 * jaka sciezka stockowy kod probowal pokazac swoj stary ekran. */
static void home_guard_cb(lv_timer_t *t)
{
    (void)t;
    if (!ui_home) return;
    lv_obj_t *cur = lv_scr_act();
    if (cur == ui_screen_time || cur == ui_screen_sensor || cur == ui_screen_setting) {
        lv_disp_load_scr(ui_home);
    }
}

/* ============================ aktualizacje ============================ */

static void update_time(void)
{
    time_t now = time(NULL);
    struct tm tm_info;
    localtime_r(&now, &tm_info);
    char buf[8];
    if (s_time_24h) {
        strftime(buf, sizeof(buf), "%H:%M", &tm_info);
    } else {
        strftime(buf, sizeof(buf), "%I:%M", &tm_info);
    }
    if (lbl_time) lv_label_set_text(lbl_time, buf);

    /* data w biezacym jezyku, np. "Wednesday, 2 July 2026" / "Środa, 2 lipca 2026" */
    if (lbl_date) {
        char dbuf[48];
        ui_i18n_date(dbuf, sizeof(dbuf), tm_info.tm_wday, tm_info.tm_mday,
                     tm_info.tm_mon, tm_info.tm_year + 1900);
        lv_label_set_text(lbl_date, dbuf);
    }
}

static void update_sensor(enum sensor_data_type type, float val)
{
    char b[16];
    switch (type) {
    case SENSOR_DATA_CO2:      snprintf(b, sizeof(b), "%d", (int)val);  if (lbl_co2)  lv_label_set_text(lbl_co2, b);  break;
    case SENSOR_DATA_TVOC:     snprintf(b, sizeof(b), "%d", (int)val);  if (lbl_tvoc) lv_label_set_text(lbl_tvoc, b); break;
    case SENSOR_DATA_TEMP:     snprintf(b, sizeof(b), "%.1f", val);     if (lbl_temp) lv_label_set_text(lbl_temp, b); break;
    case SENSOR_DATA_HUMIDITY: snprintf(b, sizeof(b), "%.0f", val);     if (lbl_hum)  lv_label_set_text(lbl_hum, b);   break;
    default: break;
    }
}

static void update_wifi(bool connected, int8_t rssi)
{
    if (img_wifi) lv_img_set_src(img_wifi, wifi_img_for(connected, rssi));
}

static struct view_data_weather s_last_wx;
static bool s_wx_valid = false;

static void update_weather(const struct view_data_weather *wx)
{
    if (!wx || !wx->valid) return;
    if (wx != &s_last_wx) { s_last_wx = *wx; s_wx_valid = true; }  /* zapamietaj do odswiezenia jezyka */
    char b[16];
    /* Uwaga: znak stopnia (U+00B0) czesto nie jest w domyslnym zakresie fontu
     * font montserrat_28 w tym buildzie MA znak stopnia (zakres od 0xB0). */
    snprintf(b, sizeof(b), "%.0f\u00B0C", wx->temperature);
    if (lbl_wx_temp) lv_label_set_text(lbl_wx_temp, b);
    if (lbl_wx_city) lv_label_set_text(lbl_wx_city, wx->city);
    if (lbl_wx_desc) lv_label_set_text(lbl_wx_desc, ui_i18n_wmo_desc(wx->weather_code));
    if (wx_icon) {
        lv_label_set_text(wx_icon, indicator_weather_code_glyph(wx->weather_code, wx->is_day));
        lv_obj_set_style_text_color(wx_icon,
            lv_color_hex(indicator_weather_code_color(wx->weather_code, wx->is_day)), 0);
    }
}

/* ============================ obsluga eventow ============================ */
/* Uwaga: handler biegnie w zadaniu view_event_task - dokladnie tak, jak
 * stockowy __view_event_handler w indicator_view.c, ktory rowniez wola
 * funkcje LVGL bez dodatkowej blokady. Trzymamy sie tej samej konwencji. */
static void home_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    lv_port_sem_take();     /* handler biegnie w view_event_task -> chron LVGL */
    switch (id) {
    case VIEW_EVENT_TIME: {
        bool *fmt = (bool *)data;
        if (fmt) s_time_24h = *fmt;
        update_time();
        break;
    }
    case VIEW_EVENT_SENSOR_DATA: {
        struct view_data_sensor_data *sd = (struct view_data_sensor_data *)data;
        if (sd) update_sensor(sd->sensor_type, sd->vaule);
        break;
    }
    case VIEW_EVENT_WIFI_ST: {
        struct view_data_wifi_st *st = (struct view_data_wifi_st *)data;
        if (st) update_wifi(st->is_connected, st->rssi);
        break;
    }
    case VIEW_EVENT_WEATHER: {
        update_weather((struct view_data_weather *)data);
        break;
    }
    case VIEW_EVENT_DISPLAY_CFG: {
        if (data) g_disp_cfg = *(struct view_data_display *)data;
        break;
    }
    case VIEW_EVENT_TIME_CFG_UPDATE: {
        if (data) {
            g_time_cfg = *(struct view_data_time_cfg *)data;
            /* odtworz strefe POSIX (net_zone nie jest w NVS, ale indeks tak) */
            if (g_time_cfg.auto_update_zone) ui_time_reapply_zone((int)g_time_cfg.zone);
        }
        break;
    }
    default:
        break;
    }
    lv_port_sem_give();
}

/* ============================ budowa ekranu ============================ */
/* Buduje/odtwarza widgety ekranu glownego (uzywa biezacego jezyka).
 * Moze byc wolane ponownie po lv_obj_clean(ui_home) przy zmianie jezyka. */
static void build_home_widgets(void)
{
    /* ---- gorny pasek: WiFi + trybik po prawej ---- */
    lv_obj_t *gear = lv_img_create(ui_home);
    lv_img_set_src(gear, &ui_img_setting_png);
    lv_obj_align(gear, LV_ALIGN_TOP_RIGHT, -14, 14);
    lv_obj_add_flag(gear, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(gear, 12);
    lv_obj_add_event_cb(gear, gear_cb, LV_EVENT_CLICKED, NULL);

    img_wifi = lv_img_create(ui_home);
    lv_img_set_src(img_wifi, &ui_img_wifi_disconet_png);
    lv_obj_align_to(img_wifi, gear, LV_ALIGN_OUT_LEFT_MID, -14, 0);

    /* miejscowosc na gornym pasku, od lewej (dluga nazwa -> wielokropek) */
    lbl_wx_city = lv_label_create(ui_home);
    lv_label_set_text(lbl_wx_city, T(S_NO_LOCATION));
    lv_obj_set_style_text_color(lbl_wx_city, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(lbl_wx_city, &ui_font_pl_18, 0);
    lv_obj_set_width(lbl_wx_city, 330);
    lv_label_set_long_mode(lbl_wx_city, LV_LABEL_LONG_DOT);
    lv_obj_align(lbl_wx_city, LV_ALIGN_TOP_LEFT, 16, 20);

    /* ---- kontener na 4 kafelki: flex-wrap, wysrodkowany, rowne odstepy ---- */
    lv_obj_t *grid = lv_obj_create(ui_home);
    lv_obj_remove_style_all(grid);
    lv_obj_set_size(grid, 440, 300);
    lv_obj_align(grid, LV_ALIGN_TOP_MID, 0, 56);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid,
        LV_FLEX_ALIGN_SPACE_EVENLY,   /* w poziomie */
        LV_FLEX_ALIGN_CENTER,         /* w pionie w wierszu */
        LV_FLEX_ALIGN_SPACE_EVENLY);  /* odstepy miedzy wierszami */
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);

    make_tile(grid, &ui_img_co2_png,        "CO2",           "ppm",     SENSOR_DATA_CO2,      &lbl_co2);
    make_tile(grid, &ui_img_tvoc_png,       "TVOC",          "ppb",     SENSOR_DATA_TVOC,     &lbl_tvoc);
    make_tile(grid, &ui_img_temp_1_png,     T(S_TILE_TEMP),  "\u00B0C", SENSOR_DATA_TEMP,     &lbl_temp);
    make_tile(grid, &ui_img_humidity_1_png, T(S_TILE_HUM),   "%",       SENSOR_DATA_HUMIDITY, &lbl_hum);

    /* ---- dol ekranu, po srodku: godzina + data ---- */
    lv_obj_t *dt_row = lv_obj_create(ui_home);
    lv_obj_remove_style_all(dt_row);
    lv_obj_set_size(dt_row, LV_PCT(96), LV_SIZE_CONTENT);
    lv_obj_align(dt_row, LV_ALIGN_BOTTOM_MID, 0, -58);
    lv_obj_set_flex_flow(dt_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(dt_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(dt_row, 14, 0);
    lv_obj_clear_flag(dt_row, LV_OBJ_FLAG_SCROLLABLE);

    lbl_time = lv_label_create(dt_row);
    lv_label_set_text(lbl_time, "--:--");
    lv_obj_set_style_text_color(lbl_time, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(lbl_time, &lv_font_montserrat_28, 0);

    lbl_date = lv_label_create(dt_row);
    lv_label_set_text(lbl_date, "");
    lv_obj_set_style_text_color(lbl_date, lv_color_hex(0x9AA5B1), 0);
    lv_obj_set_style_text_font(lbl_date, &ui_font_pl_18, 0);

    /* ---- pod spodem, wysrodkowane: ikona pogody, temperatura, opis ---- */
    lv_obj_t *wx_row = lv_obj_create(ui_home);
    lv_obj_remove_style_all(wx_row);
    lv_obj_set_size(wx_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(wx_row, LV_ALIGN_BOTTOM_MID, 0, -14);
    lv_obj_add_flag(wx_row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(wx_row, 10);
    lv_obj_add_event_cb(wx_row, weather_row_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_set_flex_flow(wx_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(wx_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(wx_row, 10, 0);
    lv_obj_clear_flag(wx_row, LV_OBJ_FLAG_SCROLLABLE);

    /* ikona pogody (glif z fontu Weather Icons) */
    wx_icon = lv_label_create(wx_row);
    lv_obj_set_style_text_font(wx_icon, &ui_font_weather_34, 0);
    lv_obj_set_style_text_color(wx_icon, lv_color_hex(0x9AA5B1), 0);
    lv_label_set_text(wx_icon, "");

    /* temperatura */
    lbl_wx_temp = lv_label_create(wx_row);
    lv_label_set_text(lbl_wx_temp, "--\u00B0C");
    lv_obj_set_style_text_color(lbl_wx_temp, lv_color_hex(0xFFFFFF), 0);

    /* opis tekstowy */
    lbl_wx_desc = lv_label_create(wx_row);
    lv_label_set_text(lbl_wx_desc, "");
    lv_obj_set_style_text_color(lbl_wx_desc, lv_color_hex(0x9AA5B1), 0);

    /* natychmiast pokaz to, co juz wiemy (bez czekania na kolejne eventy) */
    update_time();
    if (s_wx_valid) update_weather(&s_last_wx);
}

/* Przebudowa ekranu glownego po zmianie jezyka (rejestracje eventow zostaja). */
void ui_home_apply_lang(void)
{
    if (!ui_home) return;
    lv_obj_clean(ui_home);
    build_home_widgets();
}

lv_obj_t *ui_home_create(void)
{
    ui_lang_load();   /* wczytaj zapisany jezyk (domyslnie EN) przed budowa UI */
    ui_home = lv_obj_create(NULL);
    lv_obj_set_style_text_font(ui_home, &ui_font_pl_18, 0);  /* polskie znaki */
    lv_obj_clear_flag(ui_home, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_home, lv_color_hex(0x0E131A), 0);
    lv_obj_set_style_bg_opa(ui_home, LV_OPA_COVER, 0);

    build_home_widgets();

    /* ---- subskrypcja eventow (auto-aktualizacja) - rejestrujemy raz ---- */
    esp_event_handler_register_with(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_TIME,
                                    home_event_handler, NULL);
    esp_event_handler_register_with(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_SENSOR_DATA,
                                    home_event_handler, NULL);
    esp_event_handler_register_with(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_WIFI_ST,
                                    home_event_handler, NULL);
    esp_event_handler_register_with(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_WEATHER,
                                    home_event_handler, NULL);
    esp_event_handler_register_with(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_DISPLAY_CFG,
                                    home_event_handler, NULL);
    esp_event_handler_register_with(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_TIME_CFG_UPDATE,
                                    home_event_handler, NULL);

    lv_timer_create(home_guard_cb, 250, NULL);   /* pilnuje, by nasz ekran byl domem */
    ESP_LOGI(TAG, "ekran glowny utworzony");
    return ui_home;
}
