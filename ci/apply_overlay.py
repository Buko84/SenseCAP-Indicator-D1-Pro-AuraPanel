#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Naklada nasze zmiany na oryginalny przyklad Seeeda (examples/indicator_basis),
sklonowany na PRZYPIETEJ wersji. Kazda zmiana jest deterministyczna i zglasza
blad, jesli kotwica nie zostanie znaleziona -> build swiadomie sie wywali,
zamiast po cichu zbudowac niezmodyfikowany firmware.

Uzycie:  python3 ci/apply_overlay.py <sciezka_do_examples/indicator_basis>
"""
import sys
import os

def die(msg):
    print("APPLY-OVERLAY BLAD: " + msg, file=sys.stderr)
    sys.exit(1)

def read(p):
    with open(p, "r", encoding="utf-8") as f:
        return f.read()

def write(p, s):
    with open(p, "w", encoding="utf-8") as f:
        f.write(s)

def replace_once(text, old, new, where):
    n = text.count(old)
    if n == 0:
        die("nie znaleziono kotwicy w %s:\n---\n%s\n---" % (where, old))
    if n > 1:
        die("kotwica niejednoznaczna (%d wystapien) w %s" % (n, where))
    return text.replace(old, new)

def main():
    if len(sys.argv) != 2:
        die("uzycie: apply_overlay.py <examples/indicator_basis>")
    base = sys.argv[1]
    main_dir = os.path.join(base, "main")
    if not os.path.isdir(main_dir):
        die("brak katalogu %s" % main_dir)

    # ------------------------------------------------------------------
    # 1) view_data.h  -> dopisz 4 nowe eventy tuz przed VIEW_EVENT_ALL
    # ------------------------------------------------------------------
    p = os.path.join(main_dir, "view_data.h")
    t = read(p)
    if "VIEW_EVENT_WEATHER" in t:
        print("view_data.h: juz zpatchowany, pomijam")
    else:
        anchor = "    VIEW_EVENT_ALL,"
        addition = (
            "    VIEW_EVENT_WEATHER,             // struct view_data_weather\n"
            "    VIEW_EVENT_WEATHER_FORECAST,    // struct view_data_weather_forecast\n"
            "    VIEW_EVENT_CITY_SEARCH_REQ,     // char name[.]\n"
            "    VIEW_EVENT_CITY_SEARCH_RESULT,  // struct view_data_city_list\n"
            "    VIEW_EVENT_CITY_SELECT,         // struct view_data_city_item\n"
            "    VIEW_EVENT_ALL,"
        )
        t = replace_once(t, anchor, addition, "view_data.h")
        write(p, t)
        print("view_data.h: dodano 4 eventy")

    # ------------------------------------------------------------------
    # 2) indicator_model.c -> include + indicator_weather_init()
    # ------------------------------------------------------------------
    p = os.path.join(main_dir, "model", "indicator_model.c")
    t = read(p)
    if "indicator_weather.h" not in t:
        t = replace_once(t,
            '#include "indicator_city.h"',
            '#include "indicator_city.h"\n#include "indicator_weather.h"',
            "indicator_model.c (include)")
    if "indicator_weather_init()" not in t:
        t = replace_once(t,
            "    indicator_city_init();",
            "    indicator_city_init();\n    indicator_weather_init();",
            "indicator_model.c (init)")
    write(p, t)
    print("indicator_model.c: wpieto modul pogody")

    # ------------------------------------------------------------------
    # 3) indicator_view.c -> include ui_home.h + utworz i zaladuj ekran
    # ------------------------------------------------------------------
    p = os.path.join(main_dir, "view", "indicator_view.c")
    t = read(p)
    if '#include "ui_home.h"' not in t:
        t = replace_once(t,
            '#include "ui.h"\n#include "ui_helpers.h"',
            '#include "ui.h"\n#include "ui_helpers.h"\n#include "ui_home.h"',
            "indicator_view.c (include)")
    if "ui_home_create()" not in t:
        t = replace_once(t,
            "int indicator_view_init(void)\n{\n    ui_init();",
            "int indicator_view_init(void)\n{\n    ui_init();\n"
            "    ui_home_create();\n    lv_disp_load_scr(ui_home);",
            "indicator_view.c (init)")
    write(p, t)
    print("indicator_view.c: nowy ekran ustawiony jako startowy")

    # ------------------------------------------------------------------
    # 3b) indicator_time.c -> popraw odwrocona logike DST (stockowy //todo):
    #     wlaczony czas letni ma DODAWAC godzine, nie odejmowac.
    # ------------------------------------------------------------------
    p = os.path.join(main_dir, "model", "indicator_time.c")
    t = read(p)
    if "zone -=1;" in t:
        t = replace_once(t, "zone -=1;", "zone +=1;", "indicator_time.c (DST)")
        write(p, t)
        print("indicator_time.c: poprawiono logike DST (+1h)")
    else:
        print("indicator_time.c: DST juz poprawione/nieobecne")

    # ------------------------------------------------------------------
    # 4) ui.c -> include ui_home.h + przekierowanie CALEJ nawigacji "do domu"
    #    (stockowe ekrany ui_screen_time / ui_screen_sensor) na nasz ui_home.
    #    Uwaga: inicjalny lv_disp_load_scr(ui_screen_time) w ui_init() ZOSTAWIAMY
    #    (ui_home nie istnieje jeszcze w trakcie ui_init) - domem zajmuje sie
    #    ui_home_create()+load po ui_init() oraz "straznik" w ui_home.c.
    # ------------------------------------------------------------------
    p = os.path.join(main_dir, "ui", "ui.c")
    t = read(p)
    if '#include "ui_home.h"' not in t:
        t = replace_once(t,
            '#include "ui.h"\n#include "ui_helpers.h"',
            '#include "ui.h"\n#include "ui_helpers.h"\n#include "ui_home.h"',
            "ui.c (include)")

    n = 0
    for old, new in [
        ("_ui_screen_change( ui_screen_time,",   "_ui_screen_change( ui_home,"),
        ("_ui_screen_change( ui_screen_sensor,", "_ui_screen_change( ui_home,"),
        ("ui_screen_last = ui_screen_time;",     "ui_screen_last = ui_home;"),
        ("ui_screen_last = ui_screen_sensor;",   "ui_screen_last = ui_home;"),
    ]:
        n += t.count(old)
        t = t.replace(old, new)

    # powrot z ekranu WiFi -> wroc do MOJEGO ekranu ustawien (a nie do domu).
    # g_ui_settings (z ui_settings.c, deklarowany w ui_home.h) jest NULL na
    # pierwszym starcie -> wtedy zachowanie stockowe (ui_screen_last).
    wifi_back_old = "_ui_screen_change( ui_screen_last, LV_SCR_LOAD_ANIM_OVER_TOP, 200, 0);"
    wifi_back_new = "_ui_screen_change( g_ui_settings ? g_ui_settings : ui_screen_last, LV_SCR_LOAD_ANIM_OVER_TOP, 200, 0);"
    if wifi_back_old in t:
        t = t.replace(wifi_back_old, wifi_back_new, 1)
        print("ui.c: powrot z WiFi -> ekran ustawien")
    else:
        die("ui.c: nie znaleziono powrotu z ekranu WiFi")

    write(p, t)

    if "_ui_screen_change( ui_home," not in t:
        die("ui.c: nie udalo sie przekierowac nawigacji na ui_home")
    print("ui.c: przekierowano nawigacje 'do domu' na ui_home (%d zmian w tym runie)" % n)

    # ------------------------------------------------------------------
    # 5) indicator_city.c -> nie nadpisuj strefy surowym offsetem z IP
    #    ("UTC-2.0", bez regul DST). Strefe ustawia uzytkownik (ui_time)
    #    pelnym lancuchem POSIX, wiec czas letni liczy sie automatycznie.
    # ------------------------------------------------------------------
    p = os.path.join(main_dir, "model", "indicator_city.c")
    t = read(p)
    city_old = "indicator_time_net_zone_set( zone_str );"
    if city_old in t:
        t = replace_once(t, city_old,
                          "(void)zone_str; /* strefe ustawia uzytkownik (POSIX+DST) */",
                          "indicator_city.c (strefa z IP)")
        write(p, t)
        print("indicator_city.c: wylaczono nadpisywanie strefy z IP")
    else:
        print("indicator_city.c: brak wpisu strefy z IP (pomijam)")

    print("APPLY-OVERLAY: OK")

if __name__ == "__main__":
    main()
