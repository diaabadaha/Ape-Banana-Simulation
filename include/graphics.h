#ifndef GRAPHICS_H
#define GRAPHICS_H

#include "types.h"

// Color structure for RGB values
typedef struct {
    float r, g, b;
} Color;

// Rendering helpers
void draw_filled_rectangle(float x, float y, float width, float height, Color color);
void draw_rectangle_outline(float x, float y, float width, float height, Color color, float thickness);
void draw_circle(float x, float y, float radius, Color color);
void draw_circle_outline(float x, float y, float radius, Color color, float thickness);
void draw_triangle(float x, float y, float size, Color color);
void draw_text(float x, float y, const char* text, Color color);
void draw_text_large(float x, float y, const char* text, Color color);

// Color utilities
Color get_family_color(int family_id);
Color get_energy_color(int current_energy, int max_energy);
Color interpolate_color(Color c1, Color c2, float t);

// Predefined colors
extern const Color COLOR_WHITE;
extern const Color COLOR_BLACK;
extern const Color COLOR_GRAY;
extern const Color COLOR_LIGHT_GRAY;
extern const Color COLOR_DARK_GRAY;
extern const Color COLOR_GREEN;
extern const Color COLOR_LIGHT_GREEN;
extern const Color COLOR_YELLOW;
extern const Color COLOR_RED;
extern const Color COLOR_BLUE;
extern const Color COLOR_ORANGE;
extern const Color COLOR_PURPLE;
extern const Color COLOR_PINK;
extern const Color COLOR_BEIGE;

#endif