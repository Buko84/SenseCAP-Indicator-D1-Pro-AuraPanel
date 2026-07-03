#ifndef UI_I18N_H
#define UI_I18N_H

#ifdef __cplusplus
extern "C" {
#endif

/* Jezyki interfejsu. Domyslny: angielski. */
typedef enum { LANG_EN = 0, LANG_PL = 1, LANG_COUNT } ui_lang_t;

/* Klucze tekstow UI (kolejnosc dowolna, musi zgadzac sie z tablica w .c) */
typedef enum {
    S_SETTINGS_TITLE = 0,
    S_LANGUAGE,
    S_WIFI_SETTINGS,
    S_TIME_SETTINGS,
    S_WEATHER,
    S_DISPLAY,
    S_BRIGHTNESS,
    S_ALWAYS_ON,
    S_SLEEP_AFTER,
    S_CITY_PLACEHOLDER,
    S_SEARCH,
    S_SEARCHING,
    S_ENTER_CITY,
    S_NOTHING_FOUND,
    S_CHOOSE_CITY,
    S_NO_LOCATION,
    S_TILE_TEMP,
    S_TILE_HUM,
    S_TIME_TITLE,
    S_FORMAT_24H,
    S_AUTO_NTP,
    S_TIMEZONE,
    S_NTP_SERVER,
    S_MANUAL_SET,
    S_APPLY,
    S_FORECAST_TITLE,
    S_NO_FORECAST,
    S_TODAY,
    S_LANG_ENGLISH,
    S_LANG_POLISH,
    S_KEY_COUNT
} ui_str_t;

ui_lang_t   ui_lang_get(void);
void        ui_lang_set(ui_lang_t l);   /* ustawia + zapisuje w NVS */
void        ui_lang_load(void);         /* wczytuje z NVS (domyslnie EN) */

const char *T(ui_str_t k);              /* tekst dla biezacego jezyka */
const char *ui_i18n_weekday(int wday);  /* 0 = niedziela */
const char *ui_i18n_month(int mon0);    /* 0 = styczen (PL: dopelniacz) */
const char *ui_i18n_wmo_desc(int code); /* opis kodu pogody WMO */
const char *ui_i18n_tz_name(int idx);   /* nazwa strefy czasowej */

/* Data: "Wednesday, 2 July 2026" / "Środa, 2 lipca 2026" */
void ui_i18n_date(char *buf, int n, int wday, int mday, int mon0, int year);
/* Prognoza: "Friday, 03 July" / "Piątek, 03 lipca"; dzis -> "..., today/dzisiaj" */
void ui_i18n_fc_day(char *buf, int n, int wday, int mday, int mon0, int is_today);

#ifdef __cplusplus
}
#endif
#endif
