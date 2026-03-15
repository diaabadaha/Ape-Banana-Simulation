#ifndef UTILS_H
#define UTILS_H

#include <stddef.h>

char* trim_inplace(char* s);
int str_ieq(const char* a, const char* b);
int parse_int_safe(const char* s, int* out);
int parse_double_safe(const char* s, double* out);

#endif
