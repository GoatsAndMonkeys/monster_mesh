#pragma once

#include "../freertosinc.h"
#include <stdint.h>

namespace concurrency
{

/**
 * @brief Simple wrapper around FreeRTOS API for implementing a mutex lock
 *
 * Backed by xSemaphoreCreateMutex (not a binary semaphore) so FreeRTOS
 * applies priority inheritance — if a low-priority task is holding the
 * lock and a higher-priority task asks for it, the holder is temporarily
 * bumped to the waiter's priority. This avoids the classic priority
 * inversion where e.g. a render task holds the SPI lock and the radio
 * task sits behind it for way too long.
 */
class Lock
{
  public:
    Lock();

    Lock(const Lock &) = delete;
    Lock &operator=(const Lock &) = delete;

    /// Locks the lock (blocks forever).
    //
    // Must not be called from an ISR.
    void lock();

    /// Locks the lock with a bounded wait. Returns true on success, false
    /// if the timeout elapsed first. Caller should skip the work it was
    /// going to do rather than blindly proceed.
    //
    // Must not be called from an ISR.
    bool tryLock(uint32_t timeoutMs);

    // Unlocks the lock.
    //
    // Must not be called from an ISR.
    void unlock();

    /// Debug accessors — read-only snapshots, safe to call from any task.
    /// owner() is the task handle that currently holds the lock, or
    /// nullptr if free. heldMs() returns millis() since take; 0 if free.
    void    *owner()  const { return (void *)owner_; }
    uint32_t heldMs() const;

  private:
#ifdef HAS_FREE_RTOS
    SemaphoreHandle_t handle;
#endif
    // Ownership telemetry — updated inside lock()/unlock(). Plain volatile
    // rather than atomics: only the holder writes them, and we only read
    // them from diagnostic paths where a torn read is tolerable.
    volatile void     *owner_   = nullptr;
    volatile uint32_t  takenMs_ = 0;
};

} // namespace concurrency
