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
    # 4) ui.c -> include ui_home.h + powrot z ustawien na ui_home
    #    (zmiana ograniczona TYLKO do funkcji ui_event_screen_setting)
    # ------------------------------------------------------------------
    p = os.path.join(main_dir, "ui", "ui.c")
    t = read(p)
    if '#include "ui_home.h"' not in t:
        t = replace_once(t,
            '#include "ui.h"\n#include "ui_helpers.h"',
            '#include "ui.h"\n#include "ui_helpers.h"\n#include "ui_home.h"',
            "ui.c (include)")

    start_anchor = "void ui_event_screen_setting( lv_event_t * e) {"
    end_anchor = "\nvoid ui_event_wifi__st_button_3( lv_event_t * e) {"
    si = t.find(start_anchor)
    ei = t.find(end_anchor)
    if si == -1 or ei == -1 or ei <= si:
        die("ui.c: nie odnaleziono zakresu funkcji ui_event_screen_setting")
    block = t[si:ei]
    if "ui_home" in block:
        print("ui.c: powrot juz kieruje na ui_home")
    else:
        new_block = block.replace("ui_screen_time", "ui_home")
        if new_block == block:
            die("ui.c: brak ui_screen_time do podmiany w ui_event_screen_setting")
        t = t[:si] + new_block + t[ei:]
        write(p, t)
        print("ui.c: powrot z ustawien kieruje na ekran glowny")

    print("APPLY-OVERLAY: OK")

if __name__ == "__main__":
    main()
