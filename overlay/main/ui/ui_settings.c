#include "ui_settings.h"
#include "ui_home.h"
#include "ui.h"                 /* ui_img_back_png, ui_screen_wifi, ui_screen_date_time */
#include "view_data.h"
#include "indicator_weather.h"
#include "ui_font_pl.h"

#include "esp_event.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>
#include <stdint.h>

static const char *TAG = "ui_settings";

static lv_obj_t *scr;            /* ekran ustawien */
static lv_obj_t *ta_city;        /* pole wyszukiwania miasta */
static lv_obj_t *ta_ntp;         /* pole serwera NTP         */
static lv_obj_t *kb;             /* klawiatura ekranowa      */
static lv_obj_t *results_box;    /* kontener na wyniki miast */
static lv_obj_t *status_lbl;     /* komunikaty (np. "Szukam...") */

static struct view_data_city_list s_last_list;   /* mapowanie przycisk->miasto */
static bool s_created = false;

/* ------------------------------------------------------------------ */
static void set_status(const char *txt)
{
    if (status_lbl) lv_label_set_text(status_lbl, txt ? txt : "");
}

/* --- nawigacja --- */
static void back_cb(lv_event_t *e)      { lv_disp_load_scr(ui_home); }
static void open_wifi_cb(lv_event_t *e) { if (ui_screen_wifi)      lv_disp_load_scr(ui_screen_wifi); }
static void open_time_cb(lv_event_t *e) { if (ui_screen_date_time) lv_disp_load_scr(ui_screen_date_time); }

/* --- klawiatura: pokaz/ukryj przy fokusie pola tekstowego --- */
static void ta_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *ta = lv_event_get_target(e);
    if (code == LV_EVENT_FOCUSED) {
        lv_keyboard_set_textarea(kb, ta);
        lv_obj_clear_flag(kb, LV_OBJ_FLAG_HIDDEN);
    } else if (code == LV_EVENT_DEFOCUSED || code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
        lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
        lv_keyboard_set_textarea(kb, NULL);
    }
}

/* --- wybor miasta z listy wynikow --- */
static void result_btn_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= s_last_list.cnt) return;

    struct view_data_city_item item = s_last_list.items[idx];
    esp_event_post_to(view_event_handle, VIEW_EVENT_BASE,
                      VIEW_EVENT_CITY_SELECT, &item, sizeof(item), portMAX_DELAY);

    char msg[96];
    snprintf(msg, sizeof(msg), "Wybrano: %s (pobieram pogode...)", item.name);
    set_status(msg);
    /* wyczysc liste wynikow */
    lv_obj_clean(results_box);
}

/* --- klik "Szukaj" --- */
static void search_cb(lv_event_t *e)
{
    const char *q = lv_textarea_get_text(ta_city);
    if (!q || !q[0]) { set_status("Wpisz nazwe miasta"); return; }
    char buf[32];
    strncpy(buf, q, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    set_status("Szukam...");
    esp_event_post_to(view_event_handle, VIEW_EVENT_BASE,
                      VIEW_EVENT_CITY_SEARCH_REQ, buf, sizeof(buf), portMAX_DELAY);
}

/* --- klik "Zapisz NTP" --- */
static void ntp_save_cb(lv_event_t *e)
{
    const char *s = lv_textarea_get_text(ta_ntp);
    if (!s || !s[0]) { set_status("Wpisz adres serwera NTP"); return; }
    indicator_weather_set_ntp(s);
    set_status("Zapisano serwer NTP");
}

/* --- odbior wynikow wyszukiwania (event z modelu pogody) --- */
static void settings_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (id != VIEW_EVENT_CITY_SEARCH_RESULT) return;
    struct view_data_city_list *l = (struct view_data_city_list *)data;
    if (!l) return;
    s_last_list = *l;

    lv_obj_clean(results_box);
    if (l->cnt == 0) { set_status("Nic nie znaleziono"); return; }
    set_status("Wybierz miasto:");

    for (int i = 0; i < l->cnt; i++) {
        lv_obj_t *btn = lv_btn_create(results_box);
        lv_obj_set_width(btn, LV_PCT(100));
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x27303B), 0);
        lv_obj_add_event_cb(btn, result_btn_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        lv_obj_t *lbl = lv_label_create(btn);
        char row[160];
        if (l->items[i].admin1[0])
            snprintf(row, sizeof(row), "%s, %s (%s)",
                     l->items[i].name, l->items[i].country, l->items[i].admin1);
        else
            snprintf(row, sizeof(row), "%s, %s",
                     l->items[i].name, l->items[i].country);
        lv_label_set_text(lbl, row);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
    }
}

/* ------------------------------------------------------------------ */
static lv_obj_t *section_label(lv_obj_t *parent, const char *txt)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_color(l, lv_color_hex(0x8FD0E8), 0);
    return l;
}

static lv_obj_t *make_button(lv_obj_t *parent, const char *txt, lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_width(btn, LV_PCT(100));
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x27303B), 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l = lv_label_create(btn);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_color(l, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(l);
    return btn;
}

static void build(void)
{
    scr = lv_obj_create(NULL);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0E131A), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_text_font(scr, &ui_font_pl_18, 0);  /* polskie znaki na liscie miast */

    /* --- gorny pasek: tytul + powrot --- */
    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "Ustawienia");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 16, 14);

    lv_obj_t *back = lv_img_create(scr);
    lv_img_set_src(back, &ui_img_back_png);
    lv_obj_align(back, LV_ALIGN_TOP_RIGHT, -14, 16);
    lv_obj_add_flag(back, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(back, 12);
    lv_obj_add_event_cb(back, back_cb, LV_EVENT_CLICKED, NULL);

    /* --- tresc (przewijalna kolumna) --- */
    lv_obj_t *cont = lv_obj_create(scr);
    lv_obj_set_size(cont, 452, 300);
    lv_obj_align(cont, LV_ALIGN_TOP_MID, 0, 60);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(cont, 8, 0);

    /* Sekcja: Lokalizacja / Pogoda */
    section_label(cont, "Lokalizacja / Pogoda");

    lv_obj_t *row = lv_obj_create(cont);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 8, 0);

    ta_city = lv_textarea_create(row);
    lv_textarea_set_one_line(ta_city, true);
    lv_textarea_set_placeholder_text(ta_city, "Wpisz miasto...");
    lv_obj_set_flex_grow(ta_city, 1);
    lv_obj_add_event_cb(ta_city, ta_event_cb, LV_EVENT_ALL, NULL);

    lv_obj_t *search = lv_btn_create(row);
    lv_obj_add_event_cb(search, search_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *sl = lv_label_create(search);
    lv_label_set_text(sl, "Szukaj");

    status_lbl = lv_label_create(cont);
    lv_label_set_text(status_lbl, "");
    lv_obj_set_style_text_color(status_lbl, lv_color_hex(0x9AA5B1), 0);

    results_box = lv_obj_create(cont);
    lv_obj_set_size(results_box, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(results_box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(results_box, 0, 0);
    lv_obj_set_style_pad_all(results_box, 0, 0);
    lv_obj_set_flex_flow(results_box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(results_box, 6, 0);

    /* Przejscia do stockowych ekranow (dzialaja) */
    make_button(cont, "Ustawienia WiFi", open_wifi_cb);
    make_button(cont, "Czas / data / strefa", open_time_cb);

    /* Sekcja: NTP */
    section_label(cont, "Serwer NTP");

    lv_obj_t *row2 = lv_obj_create(cont);
    lv_obj_set_size(row2, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row2, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row2, 0, 0);
    lv_obj_set_style_pad_all(row2, 0, 0);
    lv_obj_set_flex_flow(row2, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row2, 8, 0);

    ta_ntp = lv_textarea_create(row2);
    lv_textarea_set_one_line(ta_ntp, true);
    lv_textarea_set_placeholder_text(ta_ntp, "pool.ntp.org");
    lv_obj_set_flex_grow(ta_ntp, 1);
    lv_obj_add_event_cb(ta_ntp, ta_event_cb, LV_EVENT_ALL, NULL);
    {
        char cur[64];
        indicator_weather_get_ntp(cur, sizeof(cur));
        if (cur[0]) lv_textarea_set_text(ta_ntp, cur);
    }

    lv_obj_t *save = lv_btn_create(row2);
    lv_obj_add_event_cb(save, ntp_save_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *svl = lv_label_create(save);
    lv_label_set_text(svl, "Zapisz");

    /* --- klawiatura ekranowa (ukryta domyslnie) --- */
    kb = lv_keyboard_create(scr);
    lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);

    /* subskrypcja wynikow wyszukiwania */
    esp_event_handler_register_with(view_event_handle, VIEW_EVENT_BASE,
                                    VIEW_EVENT_CITY_SEARCH_RESULT,
                                    settings_event_handler, NULL);

    ESP_LOGI(TAG, "ekran ustawien utworzony");
}

void ui_settings_open(void)
{
    if (!s_created) { build(); s_created = true; }
    lv_disp_load_scr(scr);
}
