#include "ui_time.h"
#include "ui_home.h"        /* ui_home + g_time_cfg */
#include "ui_settings.h"    /* powrot do ustawien */
#include "ui_font_pl.h"
#include "ui.h"             /* ui_img_back_png */
#include "view_data.h"
#include "indicator_weather.h"  /* NTP get/set */
#include "indicator_time.h"     /* indicator_time_net_zone_set (strefa POSIX z DST) */

#include "esp_event.h"
#include "esp_log.h"
#include "lv_port.h"
#include <time.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "ui_time";

#define YEAR_MIN 2024
#define YEAR_MAX 2043

/* Nazwane strefy -> pelne lancuchy POSIX z regulami DST.
 * Dzieki temu biblioteka C sama przelacza czas letni/zimowy wg dat,
 * a indeks wybranej strefy zapisujemy w cfg.zone (trafia do NVS). */
typedef struct { const char *name; const char *posix; } tz_entry_t;
static const tz_entry_t TZ_TABLE[] = {
    { "Polska / Europa Środkowa",   "CET-1CEST,M3.5.0,M10.5.0/3"    },
    { "Europa Zach. (Londyn)",      "GMT0BST,M3.5.0/1,M10.5.0"      },
    { "Europa Wsch. (Ateny/Kijów)", "EET-2EEST,M3.5.0/3,M10.5.0/4"  },
    { "UTC (bez zmiany czasu)",     "UTC0"                          },
    { "USA Wschód (ET)",            "EST5EDT,M3.2.0,M11.1.0"        },
    { "USA Środkowy (CT)",          "CST6CDT,M3.2.0,M11.1.0"        },
    { "USA Górski (MT)",            "MST7MDT,M3.2.0,M11.1.0"        },
    { "USA Zachód (PT)",            "PST8PDT,M3.2.0,M11.1.0"        },
    { "Moskwa (MSK)",               "MSK-3"                         },
    { "Dubaj (GST)",                "GST-4"                         },
    { "Indie (IST)",                "IST-5:30"                      },
    { "Chiny (CST)",                "CST-8"                         },
    { "Japonia (JST)",              "JST-9"                         },
    { "Australia Wsch. (AEST)",     "AEST-10AEDT,M10.1.0,M4.1.0/3"  },
};
#define TZ_COUNT ((int)(sizeof(TZ_TABLE) / sizeof(TZ_TABLE[0])))

/* Ponowne zastosowanie strefy po restarcie (net_zone nie jest zapisywane w NVS,
 * ale indeks tak) - wolane z ui_home po odebraniu zapisanej konfiguracji. */
void ui_time_reapply_zone(int idx)
{
    if (idx < 0 || idx >= TZ_COUNT) return;
    indicator_time_net_zone_set((char *)TZ_TABLE[idx].posix);
}

static lv_obj_t *scr;
static lv_obj_t *cont;
static lv_obj_t *sw_24h, *sw_auto;
static lv_obj_t *dd_zone;
static lv_obj_t *manual_box;
static lv_obj_t *rl_year, *rl_month, *rl_day, *rl_hour, *rl_min;
static lv_obj_t *ta_ntp, *kb;
static bool s_created = false;

/* UTC epoch z wpisanej daty/godziny (bez zaleznosci od TZ) */
static time_t epoch_utc(int y, int mo, int d, int h, int mi)
{
    y -= (mo <= 2);
    long era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    long days = era * 146097 + (long)doe - 719468;
    return (time_t)days * 86400 + h * 3600 + mi * 60;
}

static void back_cb(lv_event_t *e) { ui_settings_open(); }

static void ta_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *ta = lv_event_get_target(e);
    if (code == LV_EVENT_FOCUSED) {
        lv_keyboard_set_textarea(kb, ta);
        lv_obj_clear_flag(kb, LV_OBJ_FLAG_HIDDEN);
        if (cont) lv_obj_set_height(cont, 195);
        lv_obj_scroll_to_view_recursive(ta, LV_ANIM_ON);
    } else if (code == LV_EVENT_DEFOCUSED || code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
        lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
        lv_keyboard_set_textarea(kb, NULL);
        if (cont) lv_obj_set_height(cont, 320);
    }
}

/* pokaz/ukryj sekcje recznego ustawiania w zaleznosci od auto */
static void refresh_manual_visibility(void)
{
    bool autoupd = lv_obj_has_state(sw_auto, LV_STATE_CHECKED);
    if (manual_box) {
        if (autoupd) lv_obj_add_flag(manual_box, LV_OBJ_FLAG_HIDDEN);
        else         lv_obj_clear_flag(manual_box, LV_OBJ_FLAG_HIDDEN);
    }
}
static void auto_changed_cb(lv_event_t *e) { refresh_manual_visibility(); }

static void apply_cb(lv_event_t *e)
{
    struct view_data_time_cfg cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.time_format_24   = lv_obj_has_state(sw_24h,  LV_STATE_CHECKED);
    cfg.auto_update      = lv_obj_has_state(sw_auto, LV_STATE_CHECKED);
    cfg.daylight         = false;                 /* DST liczone automatycznie z regul POSIX */
    cfg.auto_update_zone = true;                  /* model uzyje pelnego lancucha strefy (net_zone) */

    int zi = (int)lv_dropdown_get_selected(dd_zone);
    if (zi < 0 || zi >= TZ_COUNT) zi = 0;
    cfg.zone = (int8_t)zi;                         /* zapisujemy INDEKS strefy (trafia do NVS) */

    if (!cfg.auto_update) {
        int y  = YEAR_MIN + (int)lv_roller_get_selected(rl_year);
        int mo = (int)lv_roller_get_selected(rl_month) + 1;
        int d  = (int)lv_roller_get_selected(rl_day) + 1;
        int h  = (int)lv_roller_get_selected(rl_hour);
        int mi = (int)lv_roller_get_selected(rl_min);
        cfg.time = epoch_utc(y, mo, d, h, mi);
        cfg.set_time = true;
    }

    /* ustaw pelna strefe POSIX (z regulami DST) -> libc sam liczy czas letni/zimowy */
    indicator_time_net_zone_set((char *)TZ_TABLE[zi].posix);

    /* serwer NTP */
    const char *ntp = lv_textarea_get_text(ta_ntp);
    if (ntp && ntp[0]) indicator_weather_set_ntp(ntp);

    esp_event_post_to(view_event_handle, VIEW_EVENT_BASE,
                      VIEW_EVENT_TIME_CFG_APPLY, &cfg, sizeof(cfg), portMAX_DELAY);
    ESP_LOGI(TAG, "zastosowano ustawienia czasu (auto=%d strefa=%s)", cfg.auto_update, TZ_TABLE[zi].posix);

    ui_settings_open();   /* wroc do ustawien */
}

/* ------- pomocnicze budowanie widgetow ------- */
static lv_obj_t *row(lv_obj_t *parent, const char *txt, lv_obj_t **out_ctrl_parent)
{
    lv_obj_t *r = lv_obj_create(parent);
    lv_obj_set_size(r, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(r, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(r, 0, 0);
    lv_obj_set_style_pad_all(r, 4, 0);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *l = lv_label_create(r);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_color(l, lv_color_hex(0xFFFFFF), 0);
    if (out_ctrl_parent) *out_ctrl_parent = r;
    return r;
}

static lv_obj_t *make_roller(lv_obj_t *parent, const char *opts)
{
    lv_obj_t *rl = lv_roller_create(parent);
    lv_roller_set_options(rl, opts, LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(rl, 3);
    lv_obj_set_width(rl, 74);
    return rl;
}

static void set_rollers_now(void)
{
    time_t now = time(NULL);
    struct tm tm_i; localtime_r(&now, &tm_i);
    int y = tm_i.tm_year + 1900;
    if (y < YEAR_MIN) y = YEAR_MIN; if (y > YEAR_MAX) y = YEAR_MAX;
    lv_roller_set_selected(rl_year,  y - YEAR_MIN,      LV_ANIM_OFF);
    lv_roller_set_selected(rl_month, tm_i.tm_mon,       LV_ANIM_OFF);
    lv_roller_set_selected(rl_day,   tm_i.tm_mday - 1,  LV_ANIM_OFF);
    lv_roller_set_selected(rl_hour,  tm_i.tm_hour,      LV_ANIM_OFF);
    lv_roller_set_selected(rl_min,   tm_i.tm_min,       LV_ANIM_OFF);
}

static void build(void)
{
    scr = lv_obj_create(NULL);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0E131A), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_text_font(scr, &ui_font_pl_18, 0);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "Ustawienia czasu");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 16, 12);

    lv_obj_t *back = lv_img_create(scr);
    lv_img_set_src(back, &ui_img_back_png);
    lv_obj_align(back, LV_ALIGN_TOP_RIGHT, -14, 16);
    lv_obj_add_flag(back, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(back, 12);
    lv_obj_add_event_cb(back, back_cb, LV_EVENT_CLICKED, NULL);

    cont = lv_obj_create(scr);
    lv_obj_set_size(cont, 452, 320);
    lv_obj_align(cont, LV_ALIGN_TOP_MID, 0, 58);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(cont, 6, 0);
    lv_obj_set_scroll_dir(cont, LV_DIR_VER);

    /* 24h */
    lv_obj_t *r1; row(cont, "Format 24-godzinny", &r1);
    sw_24h = lv_switch_create(r1);
    if (g_time_cfg.time_format_24) lv_obj_add_state(sw_24h, LV_STATE_CHECKED);

    /* auto (NTP) */
    lv_obj_t *r2; row(cont, "Synchronizacja automatyczna (NTP)", &r2);
    sw_auto = lv_switch_create(r2);
    if (g_time_cfg.auto_update) lv_obj_add_state(sw_auto, LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw_auto, auto_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* strefa czasowa (nazwane strefy z automatycznym czasem letnim) */
    lv_obj_t *r4; row(cont, "Strefa czasowa", &r4);
    dd_zone = lv_dropdown_create(r4);
    lv_dropdown_set_symbol(dd_zone, NULL);   /* bez strzalki-symbolu (byl kwadracik w foncie PL) */
    {
        char opts[512]; opts[0] = '\0';
        for (int i = 0; i < TZ_COUNT; i++) {
            strcat(opts, TZ_TABLE[i].name);
            if (i < TZ_COUNT - 1) strcat(opts, "\n");
        }
        lv_dropdown_set_options(dd_zone, opts);
    }
    lv_obj_set_width(dd_zone, 250);
    {
        int zsel = (int)g_time_cfg.zone;
        if (zsel < 0 || zsel >= TZ_COUNT) zsel = 0;   /* domyslnie Polska */
        lv_dropdown_set_selected(dd_zone, zsel);
    }

    /* serwer NTP */
    lv_obj_t *r5; row(cont, "Serwer NTP", &r5);
    ta_ntp = lv_textarea_create(r5);
    lv_textarea_set_one_line(ta_ntp, true);
    lv_textarea_set_placeholder_text(ta_ntp, "pool.ntp.org");
    lv_obj_set_width(ta_ntp, 240);
    lv_obj_add_event_cb(ta_ntp, ta_event_cb, LV_EVENT_ALL, NULL);
    { char cur[64]; indicator_weather_get_ntp(cur, sizeof(cur)); if (cur[0]) lv_textarea_set_text(ta_ntp, cur); }

    /* reczne ustawienie daty i godziny */
    manual_box = lv_obj_create(cont);
    lv_obj_set_size(manual_box, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(manual_box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(manual_box, 0, 0);
    lv_obj_set_style_pad_all(manual_box, 0, 0);
    lv_obj_set_flex_flow(manual_box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(manual_box, 4, 0);

    lv_obj_t *ml = lv_label_create(manual_box);
    lv_label_set_text(ml, "Ręczne ustawienie daty i godziny:");
    lv_obj_set_style_text_color(ml, lv_color_hex(0x8FD0E8), 0);

    lv_obj_t *rollers = lv_obj_create(manual_box);
    lv_obj_set_size(rollers, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(rollers, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(rollers, 0, 0);
    lv_obj_set_style_pad_all(rollers, 0, 0);
    lv_obj_set_flex_flow(rollers, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(rollers, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(rollers, 6, 0);

    char ybuf[8 * (YEAR_MAX - YEAR_MIN + 1)]; ybuf[0] = '\0';
    for (int y = YEAR_MIN; y <= YEAR_MAX; y++) {
        char t[8]; snprintf(t, sizeof(t), "%d%s", y, (y < YEAR_MAX) ? "\n" : "");
        strncat(ybuf, t, sizeof(ybuf) - strlen(ybuf) - 1);
    }
    rl_year  = make_roller(rollers, ybuf);
    rl_month = make_roller(rollers, "01\n02\n03\n04\n05\n06\n07\n08\n09\n10\n11\n12");
    { char db[4*31]; db[0]='\0'; for(int d=1; d<=31; d++){ char t[6]; snprintf(t,sizeof(t),"%02d%s",d,d<31?"\n":""); strncat(db,t,sizeof(db)-strlen(db)-1);} rl_day = make_roller(rollers, db); }
    { char hb[4*24]; hb[0]='\0'; for(int h=0; h<24; h++){ char t[6]; snprintf(t,sizeof(t),"%02d%s",h,h<23?"\n":""); strncat(hb,t,sizeof(hb)-strlen(hb)-1);} rl_hour = make_roller(rollers, hb); }
    { char mb[4*60]; mb[0]='\0'; for(int m=0; m<60; m++){ char t[6]; snprintf(t,sizeof(t),"%02d%s",m,m<59?"\n":""); strncat(mb,t,sizeof(mb)-strlen(mb)-1);} rl_min = make_roller(rollers, mb); }

    /* Zastosuj */
    lv_obj_t *apply = lv_btn_create(cont);
    lv_obj_set_width(apply, LV_PCT(100));
    lv_obj_add_event_cb(apply, apply_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *al = lv_label_create(apply);
    lv_label_set_text(al, "Zastosuj");
    lv_obj_center(al);

    /* klawiatura (dla pola NTP) */
    kb = lv_keyboard_create(scr);
    lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_obj_set_style_text_font(kb, &lv_font_montserrat_18, 0);
    lv_obj_set_size(kb, LV_PCT(100), 220);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);

    refresh_manual_visibility();
    ESP_LOGI(TAG, "ekran czasu utworzony");
}

void ui_time_open(void)
{
    if (!s_created) { build(); s_created = true; }
    set_rollers_now();
    refresh_manual_visibility();
    lv_disp_load_scr(scr);
}
