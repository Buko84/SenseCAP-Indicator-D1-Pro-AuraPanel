#include "ui_home.h"
#include "ui.h"                 /* daje LV_IMG_DECLARE(...) dla ikon oraz ui_screen_setting */
#include "view_data.h"
#include "indicator_weather.h"

#include "esp_event.h"
#include "esp_log.h"
#include <time.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "ui_home";

/* Ikony zasiegu WiFi nie sa deklarowane w ui.h (jest tam tylko wifi_disconet),
 * a ich definicje siedza w ui_img_wifi_1/2/3_png.c -> deklarujemy je tutaj. */
LV_IMG_DECLARE(ui_img_wifi_1_png);
LV_IMG_DECLARE(ui_img_wifi_2_png);
LV_IMG_DECLARE(ui_img_wifi_3_png);

lv_obj_t *ui_home = NULL;

/* --- uchwyty do etykiet, ktore aktualizujemy w locie --- */
static lv_obj_t *lbl_time;
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
static lv_obj_t *make_tile(lv_obj_t *parent, const lv_img_dsc_t *icon,
                           const char *title, const char *unit,
                           lv_obj_t **out_value)
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

/* Klik w trybik -> ekran ustawien (istniejacy w stockowym UI) */
static void gear_cb(lv_event_t *e)
{
    if (ui_screen_setting) {
        lv_disp_load_scr(ui_screen_setting);
    }
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
    if (cur == ui_screen_time || cur == ui_screen_sensor) {
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

static void update_weather(const struct view_data_weather *wx)
{
    if (!wx || !wx->valid) return;
    char b[16];
    /* Uwaga: znak stopnia (U+00B0) czesto nie jest w domyslnym zakresie fontu
     * Montserrat w LVGL -> uzywamy samego "C". Jesli wlaczysz rozszerzony zakres
     * fontu, mozesz zmienic na "%.0f\u00B0C". */
    snprintf(b, sizeof(b), "%.0f C", wx->temperature);
    if (lbl_wx_temp) lv_label_set_text(lbl_wx_temp, b);
    if (lbl_wx_city) lv_label_set_text(lbl_wx_city, wx->city);
    if (lbl_wx_desc) lv_label_set_text(lbl_wx_desc, indicator_weather_code_desc(wx->weather_code));
    if (wx_icon)
        lv_obj_set_style_bg_color(wx_icon,
            lv_color_hex(indicator_weather_code_color(wx->weather_code, wx->is_day)), 0);
}

/* ============================ obsluga eventow ============================ */
/* Uwaga: handler biegnie w zadaniu view_event_task - dokladnie tak, jak
 * stockowy __view_event_handler w indicator_view.c, ktory rowniez wola
 * funkcje LVGL bez dodatkowej blokady. Trzymamy sie tej samej konwencji. */
static void home_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
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
    default:
        break;
    }
}

/* ============================ budowa ekranu ============================ */
lv_obj_t *ui_home_create(void)
{
    ui_home = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_home, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_home, lv_color_hex(0x0E131A), 0);
    lv_obj_set_style_bg_opa(ui_home, LV_OPA_COVER, 0);

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

    make_tile(grid, &ui_img_co2_png,        "CO2",         "ppm",  &lbl_co2);
    make_tile(grid, &ui_img_tvoc_png,       "TVOC",        "ppb",  &lbl_tvoc);
    make_tile(grid, &ui_img_temp_1_png,     "Temperatura", "C", &lbl_temp);
    make_tile(grid, &ui_img_humidity_1_png, "Wilgotnosc",  "%",    &lbl_hum);

    /* ---- dol ekranu, po srodku: godzina ---- */
    lbl_time = lv_label_create(ui_home);
    lv_label_set_text(lbl_time, "--:--");
    lv_obj_set_style_text_color(lbl_time, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(lbl_time, &lv_font_montserrat_28, 0);
    lv_obj_align(lbl_time, LV_ALIGN_BOTTOM_MID, 0, -56);

    /* ---- pod godzina: ikonka pogody + temperatura + miejscowosc ---- */
    lv_obj_t *wx_row = lv_obj_create(ui_home);
    lv_obj_remove_style_all(wx_row);
    lv_obj_set_size(wx_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(wx_row, LV_ALIGN_BOTTOM_MID, 0, -16);
    lv_obj_set_flex_flow(wx_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(wx_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(wx_row, 10, 0);
    lv_obj_clear_flag(wx_row, LV_OBJ_FLAG_SCROLLABLE);

    /* kolorowa "ikonka" pogody (do podmiany na PNG w przyszlosci) */
    wx_icon = lv_obj_create(wx_row);
    lv_obj_remove_style_all(wx_icon);
    lv_obj_set_size(wx_icon, 26, 26);
    lv_obj_set_style_radius(wx_icon, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(wx_icon, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(wx_icon, lv_color_hex(0x9AA5B1), 0);

    lbl_wx_temp = lv_label_create(wx_row);
    lv_label_set_text(lbl_wx_temp, "-- C");
    lv_obj_set_style_text_color(lbl_wx_temp, lv_color_hex(0xFFFFFF), 0);

    lbl_wx_city = lv_label_create(wx_row);
    lv_label_set_text(lbl_wx_city, "(brak lokalizacji)");
    lv_obj_set_style_text_color(lbl_wx_city, lv_color_hex(0x9AA5B1), 0);

    lbl_wx_desc = lv_label_create(wx_row);
    lv_label_set_text(lbl_wx_desc, "");
    lv_obj_set_style_text_color(lbl_wx_desc, lv_color_hex(0x6B7684), 0);

    /* ---- subskrypcja eventow (auto-aktualizacja) ---- */
    esp_event_handler_register_with(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_TIME,
                                    home_event_handler, NULL);
    esp_event_handler_register_with(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_SENSOR_DATA,
                                    home_event_handler, NULL);
    esp_event_handler_register_with(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_WIFI_ST,
                                    home_event_handler, NULL);
    esp_event_handler_register_with(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_WEATHER,
                                    home_event_handler, NULL);

    update_time();
    lv_timer_create(home_guard_cb, 250, NULL);   /* pilnuje, by nasz ekran byl domem */
    ESP_LOGI(TAG, "ekran glowny utworzony");
    return ui_home;
}
