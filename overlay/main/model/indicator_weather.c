#include "indicator_weather.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"

#include "esp_log.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"
#include "nvs.h"
#include "nvs_flash.h"

#include <string.h>
#include <stdlib.h>

static const char *TAG = "weather";

#define HTTP_BUF_MAX       6144
#define NVS_NS             "weather"
#define NVS_KEY_LAT        "lat"
#define NVS_KEY_LON        "lon"
#define NVS_KEY_CITY       "city"

/* --- Open-Meteo endpointy (HTTPS, bez klucza) --- */
#define URL_GEOCODE  "https://geocoding-api.open-meteo.com/v1/search?count=%d&language=pl&format=json&name=%s"
#define URL_FORECAST "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f&current=temperature_2m,weather_code,is_day&timezone=auto"

/* --- stan modulu --- */
static struct {
    float lat;
    float lon;
    char  city[64];
    char  ip_city[64];   /* nazwa miasta z geolokalizacji IP (auto-bootstrap) */
    bool  has_location;
    bool  network_ok;
} s_ctx;

static SemaphoreHandle_t s_http_mutex;          /* serializuje uzycie bufora HTTP */
static char             *s_http_buf;            /* bufor odpowiedzi (PSRAM/heap)  */
static int               s_http_len;

/* ========================================================================
 *  HTTP GET z akumulacja odpowiedzi (jak w indicator_city.c)
 * ==================================================================== */
static esp_err_t __http_event(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
    case HTTP_EVENT_ON_DATA:
        if (s_http_buf && (s_http_len + evt->data_len) < (HTTP_BUF_MAX - 1)) {
            memcpy(s_http_buf + s_http_len, evt->data, evt->data_len);
            s_http_len += evt->data_len;
            s_http_buf[s_http_len] = '\0';
        }
        break;
    default:
        break;
    }
    return ESP_OK;
}

/* Zwraca 0 przy sukcesie; odpowiedz w s_http_buf / s_http_len. Trzeba trzymac
 * s_http_mutex przed wywolaniem. */
static int __http_get(const char *url)
{
    s_http_len = 0;
    if (s_http_buf) s_http_buf[0] = '\0';

    esp_http_client_config_t cfg = {
        .url            = url,
        .event_handler  = __http_event,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms     = 10000,
        .buffer_size    = 1024,
    };
    esp_http_client_handle_t cli = esp_http_client_init(&cfg);
    if (!cli) return -1;

    int ret = 0;
    esp_err_t err = esp_http_client_perform(cli);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "HTTP err: %s", esp_err_to_name(err));
        ret = -2;
    } else {
        int status = esp_http_client_get_status_code(cli);
        if (status != 200) {
            ESP_LOGW(TAG, "HTTP status %d", status);
            ret = -3;
        }
    }
    esp_http_client_cleanup(cli);
    return ret;
}

/* ========================================================================
 *  NVS: zapis / odczyt wybranej lokalizacji
 * ==================================================================== */
static void __location_save(float lat, float lon, const char *city)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_blob(h, NVS_KEY_LAT, &lat, sizeof(lat));
    nvs_set_blob(h, NVS_KEY_LON, &lon, sizeof(lon));
    nvs_set_str (h, NVS_KEY_CITY, city ? city : "");
    nvs_commit(h);
    nvs_close(h);
}

static void __location_load(void)
{
    nvs_handle_t h;
    s_ctx.has_location = false;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return;

    size_t sz = sizeof(float);
    bool ok = (nvs_get_blob(h, NVS_KEY_LAT, &s_ctx.lat, &sz) == ESP_OK);
    sz = sizeof(float);
    ok = ok && (nvs_get_blob(h, NVS_KEY_LON, &s_ctx.lon, &sz) == ESP_OK);
    sz = sizeof(s_ctx.city);
    nvs_get_str(h, NVS_KEY_CITY, s_ctx.city, &sz);
    nvs_close(h);

    if (ok) {
        s_ctx.has_location = true;
        ESP_LOGI(TAG, "Wczytano lokalizacje: %s (%.3f, %.3f)",
                 s_ctx.city, s_ctx.lat, s_ctx.lon);
    }
}

/* ========================================================================
 *  Geokodowanie: nazwa miasta -> lista wynikow -> VIEW_EVENT_CITY_SEARCH_RESULT
 * ==================================================================== */
/* prosty percent-encode dla zapytania (spacje, znaki spoza ASCII) */
static void __url_encode(const char *in, char *out, size_t out_sz)
{
    static const char *hex = "0123456789ABCDEF";
    size_t o = 0;
    for (const unsigned char *p = (const unsigned char *)in; *p && o + 4 < out_sz; p++) {
        unsigned char c = *p;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.') {
            out[o++] = c;
        } else {
            out[o++] = '%';
            out[o++] = hex[(c >> 4) & 0xF];
            out[o++] = hex[c & 0xF];
        }
    }
    out[o] = '\0';
}

static void __do_city_search(const char *name)
{
    char enc[96];
    char url[256];
    __url_encode(name, enc, sizeof(enc));
    snprintf(url, sizeof(url), URL_GEOCODE, WEATHER_CITY_LIST_SIZE, enc);

    struct view_data_city_list list = {0};

    xSemaphoreTake(s_http_mutex, portMAX_DELAY);
    if (__http_get(url) == 0 && s_http_len > 0) {
        cJSON *root = cJSON_Parse(s_http_buf);
        if (root) {
            cJSON *results = cJSON_GetObjectItem(root, "results");
            int n = cJSON_GetArraySize(results);
            for (int i = 0; i < n && list.cnt < WEATHER_CITY_LIST_SIZE; i++) {
                cJSON *it = cJSON_GetArrayItem(results, i);
                if (!it) continue;
                struct view_data_city_item *dst = &list.items[list.cnt];

                cJSON *jn = cJSON_GetObjectItem(it, "name");
                cJSON *jc = cJSON_GetObjectItem(it, "country");
                cJSON *ja = cJSON_GetObjectItem(it, "admin1");
                cJSON *jlat = cJSON_GetObjectItem(it, "latitude");
                cJSON *jlon = cJSON_GetObjectItem(it, "longitude");

                if (jn && jn->valuestring)
                    strncpy(dst->name, jn->valuestring, sizeof(dst->name) - 1);
                if (jc && jc->valuestring)
                    strncpy(dst->country, jc->valuestring, sizeof(dst->country) - 1);
                if (ja && ja->valuestring)
                    strncpy(dst->admin1, ja->valuestring, sizeof(dst->admin1) - 1);
                if (cJSON_IsNumber(jlat)) dst->lat = (float)jlat->valuedouble;
                if (cJSON_IsNumber(jlon)) dst->lon = (float)jlon->valuedouble;
                list.cnt++;
            }
            cJSON_Delete(root);
        }
    }
    xSemaphoreGive(s_http_mutex);

    ESP_LOGI(TAG, "Wyszukiwanie '%s' -> %d wynikow", name, list.cnt);
    esp_event_post_to(view_event_handle, VIEW_EVENT_BASE,
                      VIEW_EVENT_CITY_SEARCH_RESULT, &list, sizeof(list), portMAX_DELAY);
}

/* ========================================================================
 *  Geokodowanie pojedyncze (auto-bootstrap z geolokalizacji IP):
 *  nazwa -> pierwszy wynik -> ustawia lat/lon/city w s_ctx (bez zapisu do NVS,
 *  zeby reczny wybor miasta mial pierwszenstwo i to on trafial do NVS).
 * ==================================================================== */
static int __geocode_first(const char *name)
{
    char enc[96], url[256];
    __url_encode(name, enc, sizeof(enc));
    snprintf(url, sizeof(url), URL_GEOCODE, 1, enc);

    int ret = -1;
    xSemaphoreTake(s_http_mutex, portMAX_DELAY);
    if (__http_get(url) == 0 && s_http_len > 0) {
        cJSON *root = cJSON_Parse(s_http_buf);
        if (root) {
            cJSON *results = cJSON_GetObjectItem(root, "results");
            cJSON *it = cJSON_GetArrayItem(results, 0);
            if (it) {
                cJSON *jn   = cJSON_GetObjectItem(it, "name");
                cJSON *jlat = cJSON_GetObjectItem(it, "latitude");
                cJSON *jlon = cJSON_GetObjectItem(it, "longitude");
                if (cJSON_IsNumber(jlat) && cJSON_IsNumber(jlon)) {
                    s_ctx.lat = (float)jlat->valuedouble;
                    s_ctx.lon = (float)jlon->valuedouble;
                    if (jn && jn->valuestring)
                        strncpy(s_ctx.city, jn->valuestring, sizeof(s_ctx.city) - 1);
                    s_ctx.has_location = true;
                    ret = 0;
                }
            }
            cJSON_Delete(root);
        }
    }
    xSemaphoreGive(s_http_mutex);
    return ret;
}

/* Jesli nie mamy jeszcze lokalizacji, a znamy miasto z IP i jest siec -> ustal ja. */
static void __ensure_location(void)
{
    if (!s_ctx.has_location && s_ctx.network_ok && s_ctx.ip_city[0]) {
        ESP_LOGI(TAG, "Auto-lokalizacja z IP: %s", s_ctx.ip_city);
        __geocode_first(s_ctx.ip_city);
    }
}

/* Pobranie biezacej pogody -> VIEW_EVENT_WEATHER */
void indicator_weather_refresh(void)
{
    __ensure_location();
    if (!s_ctx.has_location || !s_ctx.network_ok) {
        ESP_LOGD(TAG, "refresh pominiety (location=%d net=%d)",
                 s_ctx.has_location, s_ctx.network_ok);
        return;
    }

    char url[256];
    snprintf(url, sizeof(url), URL_FORECAST, s_ctx.lat, s_ctx.lon);

    struct view_data_weather wx = {0};
    strncpy(wx.city, s_ctx.city, sizeof(wx.city) - 1);

    xSemaphoreTake(s_http_mutex, portMAX_DELAY);
    if (__http_get(url) == 0 && s_http_len > 0) {
        cJSON *root = cJSON_Parse(s_http_buf);
        if (root) {
            cJSON *cur = cJSON_GetObjectItem(root, "current");
            if (cur) {
                cJSON *t  = cJSON_GetObjectItem(cur, "temperature_2m");
                cJSON *wc = cJSON_GetObjectItem(cur, "weather_code");
                cJSON *dy = cJSON_GetObjectItem(cur, "is_day");
                if (cJSON_IsNumber(t))  wx.temperature  = (float)t->valuedouble;
                if (cJSON_IsNumber(wc)) wx.weather_code = wc->valueint;
                if (cJSON_IsNumber(dy)) wx.is_day       = (dy->valueint != 0);
                wx.valid = true;
            }
            cJSON_Delete(root);
        }
    }
    xSemaphoreGive(s_http_mutex);

    if (wx.valid) {
        ESP_LOGI(TAG, "Pogoda %s: %.1f C, kod %d", wx.city, wx.temperature, wx.weather_code);
        esp_event_post_to(view_event_handle, VIEW_EVENT_BASE,
                          VIEW_EVENT_WEATHER, &wx, sizeof(wx), portMAX_DELAY);
    }
}

/* ========================================================================
 *  Obsluga eventow z UI / modelu
 * ==================================================================== */
static void __event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    switch (id) {

    case VIEW_EVENT_CITY_SEARCH_REQ: {
        const char *name = (const char *)data;
        if (name && name[0]) __do_city_search(name);
        break;
    }

    case VIEW_EVENT_CITY_SELECT: {
        const struct view_data_city_item *it = (const struct view_data_city_item *)data;
        if (!it) break;
        s_ctx.lat = it->lat;
        s_ctx.lon = it->lon;
        strncpy(s_ctx.city, it->name, sizeof(s_ctx.city) - 1);
        s_ctx.has_location = true;
        __location_save(s_ctx.lat, s_ctx.lon, s_ctx.city);
        ESP_LOGI(TAG, "Wybrano miasto: %s (%.3f, %.3f)", s_ctx.city, s_ctx.lat, s_ctx.lon);
        indicator_weather_refresh();
        break;
    }

    case VIEW_EVENT_CITY: {
        /* Miasto z geolokalizacji IP (rozglaszane przez stockowy indicator_city).
         * Uzywamy go tylko, gdy uzytkownik nie wybral wlasnego miasta. */
        const char *name = (const char *)data;
        if (name && name[0]) {
            strncpy(s_ctx.ip_city, name, sizeof(s_ctx.ip_city) - 1);
            if (!s_ctx.has_location && s_ctx.network_ok) {
                if (__geocode_first(name) == 0) indicator_weather_refresh();
            }
        }
        break;
    }

    case VIEW_EVENT_WIFI_ST: {
        const struct view_data_wifi_st *st = (const struct view_data_wifi_st *)data;
        bool prev = s_ctx.network_ok;
        s_ctx.network_ok = st && st->is_network;   /* jest polaczenie z siecia */
        if (!prev && s_ctx.network_ok) {
            /* dopiero co pojawil sie internet -> odswiez pogode */
            indicator_weather_refresh();
        }
        break;
    }

    default:
        break;
    }
}

/* Okresowe odswiezanie */
static void __weather_task(void *arg)
{
    /* poczekaj chwile po starcie, zeby sie polaczyc z WiFi */
    vTaskDelay(pdMS_TO_TICKS(15000));
    while (1) {
        indicator_weather_refresh();
        vTaskDelay(pdMS_TO_TICKS(WEATHER_REFRESH_MIN * 60 * 1000));
    }
}

int indicator_weather_init(void)
{
    memset(&s_ctx, 0, sizeof(s_ctx));
    s_http_mutex = xSemaphoreCreateMutex();
    s_http_buf   = heap_caps_malloc(HTTP_BUF_MAX, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_http_buf) s_http_buf = malloc(HTTP_BUF_MAX);   /* fallback na DRAM */

    __location_load();

    ESP_ERROR_CHECK(esp_event_handler_register_with(
        view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_CITY_SEARCH_REQ,
        __event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register_with(
        view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_CITY_SELECT,
        __event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register_with(
        view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_WIFI_ST,
        __event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register_with(
        view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_CITY,
        __event_handler, NULL));

    xTaskCreatePinnedToCore(__weather_task, "weather", 6144, NULL, 4, NULL, tskNO_AFFINITY);
    return 0;
}

/* ========================================================================
 *  Mapowania kodow WMO
 * ==================================================================== */
const char *indicator_weather_code_desc(int c)
{
    switch (c) {
    case 0:                 return "Bezchmurnie";
    case 1: case 2:         return "Czesciowe zachmurzenie";
    case 3:                 return "Pochmurno";
    case 45: case 48:       return "Mgla";
    case 51: case 53: case 55: return "Mzawka";
    case 61: case 63: case 65: return "Deszcz";
    case 66: case 67:       return "Marznacy deszcz";
    case 71: case 73: case 75: case 77: return "Snieg";
    case 80: case 81: case 82: return "Przelotny deszcz";
    case 85: case 86:       return "Przelotny snieg";
    case 95:                return "Burza";
    case 96: case 99:       return "Burza z gradem";
    default:                return "---";
    }
}

uint32_t indicator_weather_code_color(int c, bool day)
{
    switch (c) {
    case 0:                 return day ? 0xFFC531 : 0x3B4CCA; /* slonce / czysta noc */
    case 1: case 2:         return 0xF4B942;
    case 3: case 45: case 48: return 0x9AA5B1;                 /* pochmurno / mgla   */
    case 51: case 53: case 55:
    case 61: case 63: case 65:
    case 80: case 81: case 82: return 0x3E9BE0;                /* deszcz             */
    case 66: case 67:
    case 71: case 73: case 75: case 77:
    case 85: case 86:       return 0x8FD0E8;                   /* snieg              */
    case 95: case 96: case 99: return 0x7A5FB0;                /* burza              */
    default:                return 0x9AA5B1;
    }
}
