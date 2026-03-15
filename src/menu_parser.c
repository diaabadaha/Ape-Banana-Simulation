#include "menu_parser.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void menu_init(Menu* m) {
    m->count = 0;
    m->capacity = 8;
    m->items = (MenuItem*)calloc((size_t)m->capacity, sizeof(MenuItem));
}

static void menu_push(Menu* m, const MenuItem* item) {
    if (m->count >= m->capacity) {
        m->capacity *= 2;
        m->items = (MenuItem*)realloc(m->items, (size_t)m->capacity * sizeof(MenuItem));
    }
    m->items[m->count++] = *item;
}

Menu parse_menu(const char* filename) {
    Menu menu;
    menu_init(&menu);

    FILE* f = fopen(filename, "r");
    if (!f) {
        // If menu missing: create minimal fallback menu in memory
        MenuItem a = {1, "Run Simulation (Pure Threading)", "run_threading_mode", 1};
        MenuItem b = {2, "Run Simulation (Hybrid Multi-process)", "run_hybrid_mode", 1};
        MenuItem c = {3, "Compare Results", "compare_results", 1};
        MenuItem d = {4, "Configure Parameters", "edit_config", 1};
        MenuItem e = {5, "Exit", "exit_program", 1};
        menu_push(&menu, &a);
        menu_push(&menu, &b);
        menu_push(&menu, &c);
        menu_push(&menu, &d);
        menu_push(&menu, &e);
        return menu;
    }

    char line[256];
    MenuItem current;
    int in_item = 0;

    while (fgets(line, sizeof(line), f)) {
        char* s = trim_inplace(line);
        if (*s == '\0' || *s == '#') continue;

        if (str_ieq(s, "[MENU_ITEM]")) {
            memset(&current, 0, sizeof(current));
            current.enabled = 1;
            in_item = 1;
            continue;
        }

        if (!in_item) continue;

        char* eq = strchr(s, '=');
        if (!eq) continue;
        *eq = '\0';

        char* key = trim_inplace(s);
        char* val = trim_inplace(eq + 1);

        if (str_ieq(key, "id")) {
            int id;
            if (parse_int_safe(val, &id)) current.id = id;
        } else if (str_ieq(key, "text")) {
            strncpy(current.text, val, sizeof(current.text) - 1);
        } else if (str_ieq(key, "function")) {
            strncpy(current.function, val, sizeof(current.function) - 1);
        } else if (str_ieq(key, "enabled")) {
            if (str_ieq(val, "true") || str_ieq(val, "1")) current.enabled = 1;
            else current.enabled = 0;
        }

        // when we have id+text+function, we can push on next [MENU_ITEM] or EOF
    }

    fclose(f);

    // second pass to push: easiest is re-read file but keep simple:
    // We'll instead require that file has one item block after another and push at EOF if last parsed.
    // To do this properly, we reopen and parse with push on block boundaries.

    // Re-parse properly:
    free(menu.items);
    menu_init(&menu);

    f = fopen(filename, "r");
    if (!f) return menu;

    in_item = 0;
    memset(&current, 0, sizeof(current));
    current.enabled = 1;

    while (fgets(line, sizeof(line), f)) {
        char* t = trim_inplace(line);
        if (*t == '\0' || *t == '#') continue;

        if (str_ieq(t, "[MENU_ITEM]")) {
            if (in_item && current.id != 0 && current.function[0] != '\0') {
                menu_push(&menu, &current);
            }
            memset(&current, 0, sizeof(current));
            current.enabled = 1;
            in_item = 1;
            continue;
        }

        if (!in_item) continue;

        char* eq2 = strchr(t, '=');
        if (!eq2) continue;
        *eq2 = '\0';

        char* key2 = trim_inplace(t);
        char* val2 = trim_inplace(eq2 + 1);

        if (str_ieq(key2, "id")) {
            int id2;
            if (parse_int_safe(val2, &id2)) current.id = id2;
        } else if (str_ieq(key2, "text")) {
            strncpy(current.text, val2, sizeof(current.text) - 1);
        } else if (str_ieq(key2, "function")) {
            strncpy(current.function, val2, sizeof(current.function) - 1);
        } else if (str_ieq(key2, "enabled")) {
            if (str_ieq(val2, "true") || str_ieq(val2, "1")) current.enabled = 1;
            else current.enabled = 0;
        }
    }

    if (in_item && current.id != 0 && current.function[0] != '\0') {
        menu_push(&menu, &current);
    }

    fclose(f);
    return menu;
}

void display_menu(const Menu* menu) {
    printf("\n========== MENU ==========\n");
    for (int i = 0; i < menu->count; i++) {
        const MenuItem* it = &menu->items[i];
        if (it->enabled) {
            printf("%d) %s\n", it->id, it->text);
        }
    }
    printf("==========================\n");
}

void free_menu(Menu* menu) {
    if (!menu) return;
    free(menu->items);
    menu->items = NULL;
    menu->count = 0;
    menu->capacity = 0;
}
