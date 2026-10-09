// SPDX-License-Identifier: GPL-3.0-or-later
// Host-only logging shim for the isolated Vulkan AA test; never on game include paths.
#pragma once
#include <cstdarg>
#include <cstdio>
#define ANDROID_LOG_INFO 4
#define ANDROID_LOG_WARN 5
#define ANDROID_LOG_ERROR 6
inline int __android_log_print(int, const char *, const char *format, ...) {
    va_list args;
    va_start(args, format);
    const int result = std::vfprintf(stderr, format, args);
    va_end(args);
    std::fputc('\n', stderr);
    return result;
}
