#pragma once
//host stand-in for the NDK logging header used by the Neo Geo core
#include <cstdio>
#define ANDROID_LOG_INFO 4
#define ANDROID_LOG_WARN 5
#define ANDROID_LOG_ERROR 6
#define __android_log_print(prio, tag, ...) (std::fprintf(stderr, "[%s] ", tag), std::fprintf(stderr, __VA_ARGS__), std::fprintf(stderr, "\n"), 0)
