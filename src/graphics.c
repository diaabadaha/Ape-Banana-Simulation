#include "../include/graphics.h"
#include <GL/glut.h>
#include <string.h>
#include <math.h>

// Predefined colors
const Color COLOR_WHITE = {1.0f, 1.0f, 1.0f};
const Color COLOR_BLACK = {0.0f, 0.0f, 0.0f};
const Color COLOR_GRAY = {0.5f, 0.5f, 0.5f};
const Color COLOR_LIGHT_GRAY = {0.8f, 0.8f, 0.8f};
const Color COLOR_DARK_GRAY = {0.2f, 0.2f, 0.2f};
const Color COLOR_GREEN = {0.0f, 0.8f, 0.0f};
const Color COLOR_LIGHT_GREEN = {0.7f, 0.9f, 0.7f};
const Color COLOR_YELLOW = {1.0f, 0.9f, 0.0f};
const Color COLOR_RED = {0.9f, 0.2f, 0.2f};
const Color COLOR_BLUE = {0.2f, 0.4f, 0.9f};
const Color COLOR_ORANGE = {1.0f, 0.6f, 0.0f};
const Color COLOR_PURPLE = {0.7f, 0.2f, 0.9f};
const Color COLOR_PINK = {1.0f, 0.6f, 0.8f};
const Color COLOR_BEIGE = {0.96f, 0.96f, 0.86f};

// Draw filled rectangle
void draw_filled_rectangle(float x, float y, float width, float height, Color color) {
    glColor3f(color.r, color.g, color.b);
    glBegin(GL_QUADS);
    glVertex2f(x, y);
    glVertex2f(x + width, y);
    glVertex2f(x + width, y + height);
    glVertex2f(x, y + height);
    glEnd();
}

// Draw rectangle outline
void draw_rectangle_outline(float x, float y, float width, float height, Color color, float thickness) {
    glColor3f(color.r, color.g, color.b);
    glLineWidth(thickness);
    glBegin(GL_LINE_LOOP);
    glVertex2f(x, y);
    glVertex2f(x + width, y);
    glVertex2f(x + width, y + height);
    glVertex2f(x, y + height);
    glEnd();
}

// Draw filled circle
void draw_circle(float x, float y, float radius, Color color) {
    glColor3f(color.r, color.g, color.b);
    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(x, y);
    for (int i = 0; i <= 32; i++) {
        float angle = 2.0f * 3.14159f * i / 32;
        glVertex2f(x + radius * cosf(angle), y + radius * sinf(angle));
    }
    glEnd();
}

// Draw circle outline
void draw_circle_outline(float x, float y, float radius, Color color, float thickness) {
    glColor3f(color.r, color.g, color.b);
    glLineWidth(thickness);
    glBegin(GL_LINE_LOOP);
    for (int i = 0; i < 32; i++) {
        float angle = 2.0f * 3.14159f * i / 32;
        glVertex2f(x + radius * cosf(angle), y + radius * sinf(angle));
    }
    glEnd();
}

// Draw filled triangle (pointing up)
void draw_triangle(float x, float y, float size, Color color) {
    glColor3f(color.r, color.g, color.b);
    glBegin(GL_TRIANGLES);
    glVertex2f(x, y - size * 0.5f);  // Top
    glVertex2f(x - size * 0.5f, y + size * 0.5f);  // Bottom left
    glVertex2f(x + size * 0.5f, y + size * 0.5f);  // Bottom right
    glEnd();
}

// Draw text (bitmap font - small)
void draw_text(float x, float y, const char* text, Color color) {
    glColor3f(color.r, color.g, color.b);
    glRasterPos2f(x, y);
    for (const char* c = text; *c != '\0'; c++) {
        glutBitmapCharacter(GLUT_BITMAP_8_BY_13, *c);
    }
}

// Draw text (bitmap font - large)
void draw_text_large(float x, float y, const char* text, Color color) {
    glColor3f(color.r, color.g, color.b);
    glRasterPos2f(x, y);
    for (const char* c = text; *c != '\0'; c++) {
        glutBitmapCharacter(GLUT_BITMAP_HELVETICA_18, *c);
    }
}

// Get family color (5 distinct colors)
Color get_family_color(int family_id) {
    const Color family_colors[] = {
        {0.9f, 0.2f, 0.2f},  // Red
        {0.2f, 0.4f, 0.9f},  // Blue
        {0.2f, 0.8f, 0.3f},  // Green
        {1.0f, 0.6f, 0.0f},  // Orange
        {0.7f, 0.2f, 0.9f},  // Purple
        {1.0f, 0.6f, 0.8f},  // Pink (for 6th family if needed)
        {0.0f, 0.8f, 0.8f},  // Cyan (for 7th family)
        {0.9f, 0.9f, 0.2f},  // Yellow (for 8th family)
    };
    
    int num_colors = sizeof(family_colors) / sizeof(family_colors[0]);
    return family_colors[family_id % num_colors];
}

// Get energy color (green -> yellow -> red based on percentage)
Color get_energy_color(int current_energy, int max_energy) {
    if (max_energy <= 0) return COLOR_GRAY;
    
    float ratio = (float)current_energy / (float)max_energy;
    
    if (ratio > 0.6f) {
        // Green to yellow (60% - 100%)
        float t = (1.0f - ratio) / 0.4f;
        return interpolate_color(COLOR_GREEN, COLOR_YELLOW, t);
    } else if (ratio > 0.3f) {
        // Yellow to red (30% - 60%)
        float t = (0.6f - ratio) / 0.3f;
        return interpolate_color(COLOR_YELLOW, COLOR_RED, t);
    } else {
        // Red (0% - 30%)
        return COLOR_RED;
    }
}

// Interpolate between two colors
Color interpolate_color(Color c1, Color c2, float t) {
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    
    Color result;
    result.r = c1.r + (c2.r - c1.r) * t;
    result.g = c1.g + (c2.g - c1.g) * t;
    result.b = c1.b + (c2.b - c1.b) * t;
    return result;
}