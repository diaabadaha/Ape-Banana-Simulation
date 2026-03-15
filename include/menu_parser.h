#ifndef MENU_PARSER_H
#define MENU_PARSER_H

#include "types.h"

Menu parse_menu(const char* filename);
void free_menu(Menu* menu);
void display_menu(const Menu* menu);

#endif
