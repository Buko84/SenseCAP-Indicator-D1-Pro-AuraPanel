#include "ui_settings.h"
#include "ui_home.h"
#include "ui_time.h"
#include "ui.h"                 /* ui_img_back_png, ui_screen_wifi */
#include "view_data.h"
#include "indicator_weather.h"
#include "ui_font_pl.h"
#include "lv_port.h"

#include "esp_event.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>
#include <stdint.h>

static const char *TAG = "ui_settings";

static lv_obj_t *scr;            /* ekran ustawien */
lv_obj_t *g_ui_settings = NULL;  /* widoczny dla ui.c (powrot z WiFi) */
static lv_obj_t *ta_city;        /* pole wyszukiwania miasta */
static lv_obj_t *kb;             /* klawiatura ekranowa      */
static lv_obj_t *results_box;    /* kontener na wyniki miast */
static lv_obj_t *cont;           /* przewijalna tresc (zmienia wysokosc pod klawiature) */
static lv_obj_t *status_lbl;     /* komunikaty (np. "Szukam...") */
static lv_obj_t *sl_bright;      /* suwak jasnosci */
static lv_obj_t *sw_alwayson;    /* przelacznik always-on */
static lv_obj_t *sleep_row;      /* wiersz "wylacz po" (widoczny gdy !always-on) */
static lv_obj_t *dd_sleep;       /* dropdown czasu do wygaszenia */

static const int SLEEP_MINS[6] = {1, 2, 5, 10, 15, 30};

static struct view_data_city_list s_last_list;   /* mapowanie przycisk->miasto */
static bool s_created = false;

/* ------------------------------------------------------------------ */
static void set_status(const char *txt)
{
    if (status_lbl) lv_label_set_text(status_lbl, txt ? txt : "");
}

/* --- nawigacja --- */
static void back_cb(lv_event_t *e)      { lv_disp_load_scr(ui_home); }
static void open_wifi_cb(lv_event_t *e) { if (ui_screen_wifi) lv_disp_load_scr(ui_screen_wifi); }
static void open_time_cb(lv_event_t *e) { ui_time_open(); }   /* NASZ ekran czasu */

/* --- wyswietlacz: zastosuj konfiguracje --- */
static void apply_display(void)
{
    struct view_data_display cfg;
    cfg.brightness = (int)lv_slider_get_value(sl_bright);
    cfg.sleep_mode_en = !lv_obj_has_state(sw_alwayson, LV_STATE_CHECKED);
    int idx = (int)lv_dropdown_get_selected(dd_sleep);
    if (idx < 0 || idx > 5) idx = 2;
    cfg.sleep_mode_time_min = SLEEP_MINS[idx];
    esp_event_post_to(view_event_handle, VIEW_EVENT_BASE,
                      VIEW_EVENT_DISPLAY_CFG_APPLY, &cfg, sizeof(cfg), portMAX_DELAY);
}

static void bright_changed_cb(lv_event_t *e)
{
    int v = (int)lv_slider_get_value(sl_bright);   /* podglad na zywo */
    esp_event_post_to(view_event_handle, VIEW_EVENT_BASE,
                      VIEW_EVENT_BRIGHTNESS_UPDATE, &v, sizeof(v), portMAX_DELAY);
}
static void bright_released_cb(lv_event_t *e) { apply_display(); }

static void alwayson_changed_cb(lv_event_t *e)
{
    bool always = lv_obj_has_state(sw_alwayson, LV_STATE_CHECKED);
    if (sleep_row) {
        if (always) lv_obj_add_flag(sleep_row, LV_OBJ_FLAG_HIDDEN);
        else        lv_obj_clear_flag(sleep_row, LV_OBJ_FLAG_HIDDEN);
    }
    apply_display();
}
static void sleep_changed_cb(lv_event_t *e) { apply_display(); }

/* --- klawiatura: pokaz/ukryj przy fokusie pola tekstowego --- */
static void ta_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *ta = lv_event_get_target(e);
    if (code == LV_EVENT_FOCUSED) {
        lv_keyboard_set_textarea(kb, ta);
        lv_obj_clear_flag(kb, LV_OBJ_FLAG_HIDDEN);
        /* zmniejsz obszar tresci nad klawiatura i przewin pole do widoku */
        if (cont) lv_obj_set_height(cont, 200);
        lv_obj_scroll_to_view_recursive(ta, LV_ANIM_ON);
    } else if (code == LV_EVENT_DEFOCUSED || code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
        lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
        lv_keyboard_set_textarea(kb, NULL);
        if (cont) lv_obj_set_height(cont, 410);   /* przywroc pelna wysokosc */
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

    /* wybrana nazwa trafia do pola tekstowego zamiast zapytania */
    lv_textarea_set_text(ta_city, item.name);
    set_status("");
    lv_obj_clean(results_box);
    lv_obj_add_flag(results_box, LV_OBJ_FLAG_HIDDEN);   /* schowaj nakladke */

    /* schowaj klawiature i przywroc uklad (przyciski wracaja na miejsce) */
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
    lv_keyboard_set_textarea(kb, NULL);
    if (cont) { lv_obj_set_height(cont, 410); lv_obj_scroll_to_y(cont, 0, LV_ANIM_ON); }
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

/* --- odbior wynikow wyszukiwania (event z modelu pogody) --- */
static void settings_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (id != VIEW_EVENT_CITY_SEARCH_RESULT) return;
    struct view_data_city_list *l = (struct view_data_city_list *)data;
    if (!l) return;

    lv_port_sem_take();     /* handler w view_event_task -> chron LVGL */
    s_last_list = *l;

    lv_obj_clean(results_box);
    if (l->cnt == 0) {
        set_status("Nic nie znaleziono");
        lv_obj_add_flag(results_box, LV_OBJ_FLAG_HIDDEN);
        lv_port_sem_give();
        return;
    }
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
        lv_obj_set_style_text_font(lbl, &ui_font_pl_16, 0);
    }
    /* pokaz nakladke nad pozostalymi ustawieniami */
    lv_obj_clear_flag(results_box, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(results_box);
    lv_port_sem_give();
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
    g_ui_settings = scr;   /* dla powrotu z ekranu WiFi */
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
    cont = lv_obj_create(scr);
    lv_obj_set_size(cont, 452, 410);   /* pelna wysokosc ekranu */
    lv_obj_align(cont, LV_ALIGN_TOP_MID, 0, 60);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(cont, 8, 0);
    lv_obj_set_scroll_dir(cont, LV_DIR_VER);   /* bez poziomego scrolla */

    /* 1) WiFi  2) Czas */
    make_button(cont, "Ustawienia WiFi", open_wifi_cb);
    make_button(cont, "Ustawienia czasu", open_time_cb);

    /* 3) Sekcja: Pogoda (wyszukiwarka miasta) */
    section_label(cont, "Pogoda");

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
    lv_obj_set_style_text_font(ta_city, &ui_font_pl_16, 0);
    lv_obj_add_event_cb(ta_city, ta_event_cb, LV_EVENT_ALL, NULL);

    lv_obj_t *search = lv_btn_create(row);
    lv_obj_add_event_cb(search, search_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *sl = lv_label_create(search);
    lv_label_set_text(sl, "Szukaj");

    status_lbl = lv_label_create(cont);
    lv_label_set_text(status_lbl, "");
    lv_obj_set_style_text_color(status_lbl, lv_color_hex(0x9AA5B1), 0);
    lv_obj_set_style_text_font(status_lbl, &ui_font_pl_16, 0);

    /* Wyniki wyszukiwania jako NAKLADKA (floating) - nie spycha innych ustawien.
     * Kotwiczone pod wierszem miasta; nieprzezroczyste tlo; ukryte gdy puste. */
    results_box = lv_obj_create(cont);
    lv_obj_add_flag(results_box, LV_OBJ_FLAG_FLOATING);   /* ignorowane przez uklad kolumny */
    lv_obj_set_width(results_box, 452);
    lv_obj_set_height(results_box, LV_SIZE_CONTENT);
    lv_obj_set_style_max_height(results_box, 250, 0);
    lv_obj_set_style_bg_color(results_box, lv_color_hex(0x161C24), 0);
    lv_obj_set_style_bg_opa(results_box, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(results_box, 10, 0);
    lv_obj_set_style_border_width(results_box, 1, 0);
    lv_obj_set_style_border_color(results_box, lv_color_hex(0x2A3440), 0);
    lv_obj_set_style_pad_all(results_box, 6, 0);
    lv_obj_set_flex_flow(results_box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(results_box, 6, 0);
    lv_obj_align_to(results_box, row, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 6);
    lv_obj_add_flag(results_box, LV_OBJ_FLAG_HIDDEN);

    /* 4) Sekcja: Wyswietlacz */
    section_label(cont, "Wyświetlacz");

    /* jasnosc */
    lv_obj_t *br_row = lv_obj_create(cont);
    lv_obj_set_size(br_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(br_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(br_row, 0, 0);
    lv_obj_set_style_pad_all(br_row, 4, 0);
    lv_obj_set_flex_flow(br_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(br_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *brl = lv_label_create(br_row);
    lv_label_set_text(brl, "Jasność");
    lv_obj_set_style_text_color(brl, lv_color_hex(0xFFFFFF), 0);
    sl_bright = lv_slider_create(br_row);
    lv_slider_set_range(sl_bright, 1, 100);
    lv_slider_set_value(sl_bright, g_disp_cfg.brightness, LV_ANIM_OFF);
    lv_obj_set_width(sl_bright, 240);
    lv_obj_add_event_cb(sl_bright, bright_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(sl_bright, bright_released_cb, LV_EVENT_RELEASED, NULL);

    /* always-on */
    lv_obj_t *ao_row = lv_obj_create(cont);
    lv_obj_set_size(ao_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(ao_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ao_row, 0, 0);
    lv_obj_set_style_pad_all(ao_row, 4, 0);
    lv_obj_set_flex_flow(ao_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ao_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *aol = lv_label_create(ao_row);
    lv_label_set_text(aol, "Ekran zawsze włączony");
    lv_obj_set_style_text_color(aol, lv_color_hex(0xFFFFFF), 0);
    sw_alwayson = lv_switch_create(ao_row);
    if (!g_disp_cfg.sleep_mode_en) lv_obj_add_state(sw_alwayson, LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw_alwayson, alwayson_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* czas wygaszenia (widoczny gdy nie always-on) */
    sleep_row = lv_obj_create(cont);
    lv_obj_set_size(sleep_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(sleep_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(sleep_row, 0, 0);
    lv_obj_set_style_pad_all(sleep_row, 4, 0);
    lv_obj_set_flex_flow(sleep_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(sleep_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *spl = lv_label_create(sleep_row);
    lv_label_set_text(spl, "Wyłącz podświetlenie po (min)");
    lv_obj_set_style_text_color(spl, lv_color_hex(0xFFFFFF), 0);
    dd_sleep = lv_dropdown_create(sleep_row);
    lv_dropdown_set_options(dd_sleep, "1\n2\n5\n10\n15\n30");
    lv_obj_set_style_text_font(dd_sleep, &lv_font_montserrat_18, 0);  /* strzalka jako symbol, nie kwadracik */
    lv_obj_set_width(dd_sleep, 90);
    /* wybierz najblizsza wartosc z zapisanej konfiguracji */
    {
        int sel = 2;
        for (int i = 0; i < 6; i++) if (SLEEP_MINS[i] == g_disp_cfg.sleep_mode_time_min) { sel = i; break; }
        lv_dropdown_set_selected(dd_sleep, sel);
    }
    lv_obj_add_event_cb(dd_sleep, sleep_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);
    if (!g_disp_cfg.sleep_mode_en) lv_obj_add_flag(sleep_row, LV_OBJ_FLAG_HIDDEN);

    /* --- klawiatura ekranowa (ukryta domyslnie; dla pola miasta) --- */
    kb = lv_keyboard_create(scr);
    lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_TEXT_LOWER);
    /* klawiatura musi uzywac fontu z symbolami LVGL (shift/enter/backspace) */
    lv_obj_set_style_text_font(kb, &lv_font_montserrat_18, 0);
    lv_obj_set_size(kb, LV_PCT(100), 220);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
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
