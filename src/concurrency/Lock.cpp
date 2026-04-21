#include "Lock.h"
#include "configuration.h"
#include <Arduino.h>  // millis()
#include <cassert>

namespace concurrency
{

#ifdef HAS_FREE_RTOS
// xSemaphoreCreateMutex gives a priority-inheriting mutex (unlike
// xSemaphoreCreateBinary). See Lock.h for rationale.
Lock::Lock() : handle(xSemaphoreCreateMutex())
{
    assert(handle);
    // Mutexes are created already available; no xSemaphoreGive needed.
}

void Lock::lock()
{
    if (xSemaphoreTake(handle, portMAX_DELAY) == false) {
        abort();
    }
    owner_   = (void *)xTaskGetCurrentTaskHandle();
    takenMs_ = millis();
}

bool Lock::tryLock(uint32_t timeoutMs)
{
    TickType_t ticks = timeoutMs == 0 ? 0 : pdMS_TO_TICKS(timeoutMs);
    if (xSemaphoreTake(handle, ticks) == pdTRUE) {
        owner_   = (void *)xTaskGetCurrentTaskHandle();
        takenMs_ = millis();
        return true;
    }
    return false;
}

void Lock::unlock()
{
    owner_   = nullptr;
    takenMs_ = 0;
    if (xSemaphoreGive(handle) == false) {
        abort();
    }
}

uint32_t Lock::heldMs() const
{
    uint32_t t = takenMs_;
    if (t == 0) return 0;
    uint32_t now = millis();
    return now >= t ? now - t : 0;
}
#else
Lock::Lock() {}

void Lock::lock() {}
bool Lock::tryLock(uint32_t) { return true; }
void Lock::unlock() {}
uint32_t Lock::heldMs() const { return 0; }
#endif

} // namespace concurrency
