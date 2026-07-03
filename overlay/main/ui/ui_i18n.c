#include "ui_i18n.h"

#include <stdio.h>
#include <string.h>
#include "nvs.h"
#include "esp_log.h"

static const char *TAG = "ui_i18n";

static ui_lang_t s_lang = LANG_EN;   /* domyslnie angielski */

/* --- tabela tekstow [klucz][jezyk] --- */
static const char *STR[S_KEY_COUNT][LANG_COUNT] = {
    /* S_SETTINGS_TITLE   */ { "Settings",            "Ustawienia" },
    /* S_LANGUAGE         */ { "Language",            "Język" },
    /* S_WIFI_SETTINGS    */ { "Wi-Fi settings",      "Ustawienia WiFi" },
    /* S_TIME_SETTINGS    */ { "Time settings",       "Ustawienia czasu" },
    /* S_WEATHER          */ { "Weather",             "Pogoda" },
    /* S_DISPLAY          */ { "Display",             "Wyświetlacz" },
    /* S_BRIGHTNESS       */ { "Brightness",          "Jasność" },
    /* S_ALWAYS_ON        */ { "Screen always on",    "Ekran zawsze włączony" },
    /* S_SLEEP_AFTER      */ { "Turn off backlight after (min)", "Wyłącz podświetlenie po (min)" },
    /* S_CITY_PLACEHOLDER */ { "Enter a city...",     "Wpisz miasto..." },
    /* S_SEARCH           */ { "Search",              "Szukaj" },
    /* S_SEARCHING        */ { "Searching...",        "Szukam..." },
    /* S_ENTER_CITY       */ { "Enter a city name",   "Wpisz nazwe miasta" },
    /* S_NOTHING_FOUND    */ { "Nothing found",       "Nic nie znaleziono" },
    /* S_CHOOSE_CITY      */ { "Choose a city:",      "Wybierz miasto:" },
    /* S_NO_LOCATION      */ { "(no location)",       "(brak lokalizacji)" },
    /* S_TILE_TEMP        */ { "Temperature",         "Temperatura" },
    /* S_TILE_HUM         */ { "Humidity",            "Wilgotność" },
    /* S_TIME_TITLE       */ { "Time settings",       "Ustawienia czasu" },
    /* S_FORMAT_24H       */ { "24-hour format",      "Format 24-godzinny" },
    /* S_AUTO_NTP         */ { "Automatic sync (NTP)","Synchronizacja automatyczna (NTP)" },
    /* S_TIMEZONE         */ { "Time zone",           "Strefa czasowa" },
    /* S_NTP_SERVER       */ { "NTP server",          "Serwer NTP" },
    /* S_MANUAL_SET       */ { "Manual date and time:", "Ręczne ustawienie daty i godziny:" },
    /* S_APPLY            */ { "Apply",               "Zastosuj" },
    /* S_FORECAST_TITLE   */ { "3-day forecast",      "Pogoda 3-dniowa" },
    /* S_NO_FORECAST      */ { "No forecast data (choose a city in settings).",
                              "Brak danych prognozy (wybierz miasto w ustawieniach)." },
    /* S_TODAY            */ { "today",               "dzisiaj" },
    /* S_LANG_ENGLISH     */ { "English",             "Angielski" },
    /* S_LANG_POLISH      */ { "Polish",              "Polski" },
};

/* --- dni tygodnia (0 = niedziela) --- */
static const char *WD[LANG_COUNT][7] = {
    { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" },
    { "Niedziela", "Poniedziałek", "Wtorek", "Środa", "Czwartek", "Piątek", "Sobota" },
};

/* --- miesiace (EN: mianownik, PL: dopelniacz) --- */
static const char *MO[LANG_COUNT][12] = {
    { "January", "February", "March", "April", "May", "June",
      "July", "August", "September", "October", "November", "December" },
    { "stycznia", "lutego", "marca", "kwietnia", "maja", "czerwca",
      "lipca", "sierpnia", "września", "października", "listopada", "grudnia" },
};

/* --- nazwy stref czasowych (indeksy zgodne z TZ_TABLE w ui_time.c) --- */
static const char *TZN[LANG_COUNT][14] = {
    { "Poland / Central Europe", "Western Europe (London)", "Eastern Europe (Athens/Kyiv)",
      "UTC (no DST)", "US Eastern (ET)", "US Central (CT)", "US Mountain (MT)",
      "US Pacific (PT)", "Moscow (MSK)", "Dubai (GST)", "India (IST)",
      "China (CST)", "Japan (JST)", "Australia East (AEST)" },
    { "Polska / Europa Środkowa", "Europa Zach. (Londyn)", "Europa Wsch. (Ateny/Kijów)",
      "UTC (bez zmiany czasu)", "USA Wschód (ET)", "USA Środkowy (CT)", "USA Górski (MT)",
      "USA Zachód (PT)", "Moskwa (MSK)", "Dubaj (GST)", "Indie (IST)",
      "Chiny (CST)", "Japonia (JST)", "Australia Wsch. (AEST)" },
};
#define TZN_COUNT ((int)(sizeof(TZN[0]) / sizeof(TZN[0][0])))

ui_lang_t ui_lang_get(void) { return s_lang; }

void ui_lang_set(ui_lang_t l)
{
    if (l < 0 || l >= LANG_COUNT) l = LANG_EN;
    s_lang = l;
    nvs_handle_t h;
    if (nvs_open("uicfg", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, "lang", (uint8_t)l);
        nvs_commit(h);
        nvs_close(h);
    }
    ESP_LOGI(TAG, "jezyk = %d", (int)l);
}

void ui_lang_load(void)
{
    nvs_handle_t h;
    uint8_t v = LANG_EN;
    if (nvs_open("uicfg", NVS_READONLY, &h) == ESP_OK) {
        if (nvs_get_u8(h, "lang", &v) != ESP_OK) v = LANG_EN;
        nvs_close(h);
    }
    s_lang = (v < LANG_COUNT) ? (ui_lang_t)v : LANG_EN;
}

const char *T(ui_str_t k)
{
    if (k < 0 || k >= S_KEY_COUNT) return "";
    return STR[k][s_lang];
}

const char *ui_i18n_weekday(int wday)
{
    if (wday < 0 || wday > 6) wday = 0;
    return WD[s_lang][wday];
}

const char *ui_i18n_month(int mon0)
{
    if (mon0 < 0 || mon0 > 11) mon0 = 0;
    return MO[s_lang][mon0];
}

const char *ui_i18n_tz_name(int idx)
{
    if (idx < 0 || idx >= TZN_COUNT) idx = 0;
    return TZN[s_lang][idx];
}

const char *ui_i18n_wmo_desc(int c)
{
    static const char *EN[] = {
        "Clear", "Partly cloudy", "Overcast", "Fog", "Drizzle", "Rain",
        "Freezing rain", "Snow", "Rain showers", "Snow showers",
        "Thunderstorm", "Thunderstorm with hail", "---"
    };
    static const char *PL[] = {
        "Bezchmurnie", "Częściowe zachmurzenie", "Pochmurno", "Mgła", "Mżawka", "Deszcz",
        "Marznący deszcz", "Śnieg", "Przelotny deszcz", "Przelotny śnieg",
        "Burza", "Burza z gradem", "---"
    };
    int i;
    switch (c) {
    case 0:                                   i = 0;  break;
    case 1: case 2:                           i = 1;  break;
    case 3:                                   i = 2;  break;
    case 45: case 48:                         i = 3;  break;
    case 51: case 53: case 55:                i = 4;  break;
    case 61: case 63: case 65:                i = 5;  break;
    case 66: case 67:                         i = 6;  break;
    case 71: case 73: case 75: case 77:       i = 7;  break;
    case 80: case 81: case 82:                i = 8;  break;
    case 85: case 86:                         i = 9;  break;
    case 95:                                  i = 10; break;
    case 96: case 99:                         i = 11; break;
    default:                                  i = 12; break;
    }
    return (s_lang == LANG_PL) ? PL[i] : EN[i];
}

void ui_i18n_date(char *buf, int n, int wday, int mday, int mon0, int year)
{
    snprintf(buf, n, "%s, %d %s %d",
             ui_i18n_weekday(wday), mday, ui_i18n_month(mon0), year);
}

void ui_i18n_fc_day(char *buf, int n, int wday, int mday, int mon0, int is_today)
{
    if (is_today)
        snprintf(buf, n, "%s, %s", ui_i18n_weekday(wday), T(S_TODAY));
    else
        snprintf(buf, n, "%s, %02d %s", ui_i18n_weekday(wday), mday, ui_i18n_month(mon0));
}
