/*
 * Shared helpers (see utils.h).
 */
#include "mbed.h"
#include "utils.h"

// Helper: replaces deprecated Kernel::get_ms_count() (mbed-os-6.0.0)
uint64_t now_ms(void)
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(Kernel::Clock::now().time_since_epoch()).count();
}
