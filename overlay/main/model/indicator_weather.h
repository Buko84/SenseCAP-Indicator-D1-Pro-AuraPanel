#ifndef INDICATOR_WEATHER_H
#define INDICATOR_WEATHER_H

#include "view_data.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --------------------------------------------------------------------------
 *  Modul pogody oparty o Open-Meteo (darmowe, BEZ klucza API):
 *    - geokodowanie / wyszukiwanie miast:  geocoding-api.open-meteo.com
 *    - pogoda biezaca (temp + kod WMO):     api.open-meteo.com
 *
 *  Wzorzec dokladnie taki jak w indicator_city.c: HTTP + cJSON + esp_event.
 *  Wyniki i stany sa rozglaszane po petli view_event_handle jako VIEW_EVENT_*.
 * ------------------------------------------------------------------------ */

#define WEATHER_CITY_LIST_SIZE   6
#define WEATHER_REFRESH_MIN      30      /* co ile minut odswiezac pogode */

/* Pojedynczy wynik wyszukiwania miasta */
struct view_data_city_item {
    char  name[64];       /* np. "Warszawa"                */
    char  country[48];    /* np. "Poland" / kod kraju      */
    char  admin1[64];     /* region/wojewodztwo (moze byc pusty) */
    float lat;
    float lon;
};

/* Lista wynikow wyszukiwania (do pokazania na ekranie ustawien) */
struct view_data_city_list {
    uint16_t cnt;
    struct view_data_city_item items[WEATHER_CITY_LIST_SIZE];
};

/* Biezaca pogoda dla wybranej lokalizacji */
struct view_data_weather {
    bool  valid;
    char  city[64];
    float temperature;    /* stopnie Celsjusza */
    int   weather_code;   /* kod WMO (0=bezchmurnie, 61=deszcz, ...) */
    bool  is_day;
};

/* --------------------------------------------------------------------------
 *  WAZNE: dopisz te 4 pozycje do enuma w view_data.h TUZ PRZED VIEW_EVENT_ALL:
 *
 *      VIEW_EVENT_WEATHER,             // struct view_data_weather
 *      VIEW_EVENT_CITY_SEARCH_REQ,     // char name[.]  (zapytanie o miasta)
 *      VIEW_EVENT_CITY_SEARCH_RESULT,  // struct view_data_city_list
 *      VIEW_EVENT_CITY_SELECT,         // struct view_data_city_item (wybor)
 *
 *  Kolejnosc/wartosci enuma sa uzywane jako event id, wiec dopisujemy je
 *  na koncu (przed VIEW_EVENT_ALL), zeby nie przesunac istniejacych.
 * ------------------------------------------------------------------------ */

/* Inicjalizacja modulu: wczytuje zapisana lokalizacje z NVS, rejestruje
 * obsluge eventow (wyszukiwanie/wybor miasta, zmiana stanu WiFi) i startuje
 * zadanie okresowego odswiezania. Wolaj z indicator_model_init(). */
int indicator_weather_init(void);

/* Reczne wymuszenie odswiezenia pogody (np. po wpieciu w UI). */
void indicator_weather_refresh(void);

/* Mapowanie kodu WMO -> krotki opis PL (do etykiety pod temperatura). */
const char *indicator_weather_code_desc(int wmo_code);

/* Mapowanie kodu WMO -> kolor "ikonki" pogody (0xRRGGBB), gdy nie masz
 * jeszcze wlasnych PNG-ow. Slonce=zolte, chmury=szare, deszcz=niebieski itd. */
uint32_t indicator_weather_code_color(int wmo_code, bool is_day);

#ifdef __cplusplus
}
#endif

#endif /* INDICATOR_WEATHER_H */
