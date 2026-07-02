#include "ui_forecast.h"
#include "ui_home.h"
#include "ui_font_pl.h"
#include "ui.h"                 /* ui_img_back_png */
#include "view_data.h"
#include "indicator_weather.h"

#include "esp_event.h"
#include "esp_log.h"
#include "lv_port.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "ui_forecast";

static lv_obj_t *scr;
static lv_obj_t *city_lbl;
static lv_obj_t *list_box;
static bool s_created = false;
static struct view_data_weather_forecast s_fc;

/* Nazwa dnia tygodnia (PL) z daty "YYYY-MM-DD" (algorytm Zellera, 0=Niedziela). */
static const char *pl_weekday(const char *date)
{
    int y, m, d;
    if (!date || sscanf(date, "%d-%d-%d", &y, &m, &d) != 3) return date ? date : "";
    static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    int yy = y - (m < 3);
    int w = (yy + yy/4 - yy/100 + yy/400 + t[m - 1] + d) % 7;
    static const char *names[7] = {"Niedziela", "Poniedziałek", "Wtorek",
                                    "Środa", "Czwartek", "Piątek", "Sobota"};
    if (w < 0) w += 7;
    return names[w];
}

static void back_cb(lv_event_t *e) { lv_disp_load_scr(ui_home); }

static void rebuild(void)
{
    if (!list_box) return;
    lv_obj_clean(list_box);

    if (city_lbl) lv_label_set_text(city_lbl, s_fc.city[0] ? s_fc.city : "");

    if (!s_fc.valid || s_fc.days == 0) {
        lv_obj_t *l = lv_label_create(list_box);
        lv_label_set_text(l, "Brak danych prognozy (wybierz miasto w ustawieniach).");
        lv_obj_set_style_text_color(l, lv_color_hex(0x9AA5B1), 0);
        return;
    }

    for (int i = 0; i < s_fc.days; i++) {
        struct view_data_weather_day *d = &s_fc.d[i];

        lv_obj_t *card = lv_obj_create(list_box);
        lv_obj_set_size(card, LV_PCT(100), 84);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(card, 14, 0);
        lv_obj_set_style_bg_color(card, lv_color_hex(0x1E2530), 0);
        lv_obj_set_style_border_width(card, 0, 0);
        lv_obj_set_style_pad_all(card, 12, 0);

        /* ikona pogody (glif z fontu Weather Icons, kolor wg kodu WMO) */
        lv_obj_t *ic = lv_label_create(card);
        lv_obj_set_style_text_font(ic, &ui_font_weather_34, 0);
        lv_obj_set_style_text_color(ic, lv_color_hex(indicator_weather_code_color(d->weather_code, true)), 0);
        lv_label_set_text(ic, indicator_weather_code_glyph(d->weather_code, true));
        lv_obj_align(ic, LV_ALIGN_LEFT_MID, 0, 0);

        /* dzien + opis */
        lv_obj_t *day = lv_label_create(card);
        lv_label_set_text(day, pl_weekday(d->date));
        lv_obj_set_style_text_color(day, lv_color_hex(0xFFFFFF), 0);
        lv_obj_align(day, LV_ALIGN_LEFT_MID, 44, -12);

        lv_obj_t *desc = lv_label_create(card);
        lv_label_set_text(desc, indicator_weather_code_desc(d->weather_code));
        lv_obj_set_style_text_color(desc, lv_color_hex(0x9AA5B1), 0);
        lv_obj_align(desc, LV_ALIGN_LEFT_MID, 44, 12);

        /* temperatury max/min po prawej */
        lv_obj_t *temp = lv_label_create(card);
        char b[32];
        snprintf(b, sizeof(b), "%.0f\u00B0 / %.0f\u00B0", d->tmax, d->tmin);
        lv_label_set_text(temp, b);
        lv_obj_set_style_text_color(temp, lv_color_hex(0xFFFFFF), 0);
        lv_obj_align(temp, LV_ALIGN_RIGHT_MID, 0, 0);
    }
}

static void forecast_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (id != VIEW_EVENT_WEATHER_FORECAST) return;
    struct view_data_weather_forecast *fc = (struct view_data_weather_forecast *)data;
    if (fc) {
        lv_port_sem_take();     /* handler biegnie w view_event_task -> chron LVGL */
        s_fc = *fc;
        rebuild();
        lv_port_sem_give();
    }
}

static void build(void)
{
    scr = lv_obj_create(NULL);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0E131A), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_text_font(scr, &ui_font_pl_18, 0);   /* polskie znaki wszedzie */

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "Pogoda 3-dniowa");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 16, 12);

    city_lbl = lv_label_create(scr);
    lv_label_set_text(city_lbl, "");
    lv_obj_set_style_text_color(city_lbl, lv_color_hex(0x8FD0E8), 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 16, 12);
    lv_obj_align_to(city_lbl, title, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 4);

    lv_obj_t *back = lv_img_create(scr);
    lv_img_set_src(back, &ui_img_back_png);
    lv_obj_align(back, LV_ALIGN_TOP_RIGHT, -14, 16);
    lv_obj_add_flag(back, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(back, 12);
    lv_obj_add_event_cb(back, back_cb, LV_EVENT_CLICKED, NULL);

    list_box = lv_obj_create(scr);
    lv_obj_set_size(list_box, 452, 300);
    lv_obj_align(list_box, LV_ALIGN_TOP_MID, 0, 100);
    lv_obj_set_style_bg_opa(list_box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list_box, 0, 0);
    lv_obj_set_flex_flow(list_box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list_box, 10, 0);

    esp_event_handler_register_with(view_event_handle, VIEW_EVENT_BASE,
                                    VIEW_EVENT_WEATHER_FORECAST,
                                    forecast_event_handler, NULL);
    ESP_LOGI(TAG, "ekran prognozy utworzony");
}

void ui_forecast_open(void)
{
    if (!s_created) { build(); s_created = true; }
    rebuild();
    lv_disp_load_scr(scr);
    indicator_weather_refresh();   /* odswiez dane w tle */
}
