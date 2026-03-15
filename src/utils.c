#include "utils.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

char* trim_inplace(char* s) {
    if (!s) return s;

    // left trim
    while (isspace((unsigned char)*s)) s++;

    // right trim
    char* end = s + strlen(s);
    while (end > s && isspace((unsigned char)*(end - 1))) end--;
    *end = '\0';

    return s;
}

int str_ieq(const char* a, const char* b) {
    if (!a || !b) return 0;
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
        a++; b++;
    }
    return *a == '\0' && *b == '\0';
}

int parse_int_safe(const char* s, int* out) {
    if (!s || !out) return 0;
    char* end = NULL;
    long v = strtol(s, &end, 10);
    if (end == s) return 0; // no conversion
    while (*end) {
        if (!isspace((unsigned char)*end)) return 0;
        end++;
    }
    *out = (int)v;
    return 1;
}

int parse_double_safe(const char* s, double* out) {
    if (!s || !out) return 0;
    char* end = NULL;
    double v = strtod(s, &end);
    if (end == s) return 0;
    while (*end) {
        if (!isspace((unsigned char)*end)) return 0;
        end++;
    }
    *out = v;
    return 1;
}
