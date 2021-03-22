#pragma once

#ifndef TRACY_ENABLE

#define TracyFormat(var, len, fmt, ...)

#else

#include <stdio.h>

#define TracyFormat(var, len_out, len, fmt, ...)         \
    char var[len];                                       \
    int len_out = snprintf(var, len, fmt, __VA_ARGS__);  \
    len_out = std::min(len_out, (int)(sizeof(var) - 1)); \
    CZ_ASSERT(len_out >= 0);

#endif
