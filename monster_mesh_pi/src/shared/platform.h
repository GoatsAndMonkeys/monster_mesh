#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <time.h>
// millis() via CLOCK_MONOTONIC
inline uint32_t millis() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000UL + ts.tv_nsec / 1000000UL);
}
// random(n) -> rand() % n
inline int32_t random(int32_t n) { return rand() % n; }
// min/max (avoid std::min conflict with macros)
template<typename T> inline T mmMin(T a, T b) { return a < b ? a : b; }
template<typename T> inline T mmMax(T a, T b) { return a > b ? a : b; }
// Logging
#define LOG_INFO(fmt, ...)  fprintf(stderr, "[INFO] " fmt "\n", ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)  fprintf(stderr, "[WARN] " fmt "\n", ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) fprintf(stderr, "[ERROR] " fmt "\n", ##__VA_ARGS__)
// Arduino-style bool (already defined on Linux as _Bool, but just in case)
#ifndef F
#define F(x) (x)
#endif
