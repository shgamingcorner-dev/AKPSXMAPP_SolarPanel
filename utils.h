/*
 * Shared helpers used by multiple translation units (main.cpp, tracker.cpp).
 */
#ifndef UTILS_H
#define UTILS_H

#include <cstdint>

// Milliseconds since boot. Replaces deprecated Kernel::get_ms_count().
uint64_t now_ms(void);

#endif // UTILS_H
