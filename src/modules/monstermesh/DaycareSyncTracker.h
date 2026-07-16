#pragma once

#include <stdint.h>

// Small, platform-independent state machine for event-driven daycare SAV
// persistence.  Keeping the watermark and retry deadline together makes it
// impossible for a failed attempt to accidentally consume an event.
class DaycareSyncTracker
{
  public:
    bool needsSync(uint32_t eventTime) const { return eventTime != 0 && eventTime != lastSyncedEventTime_; }

    bool retryReady(uint32_t nowMs) const
    {
        return retryAfterMs_ == 0 || static_cast<int32_t>(nowMs - retryAfterMs_) >= 0;
    }

    void recordFailure(uint32_t nowMs, uint32_t backoffMs)
    {
        deferRetry(nowMs, backoffMs);
    }

    void deferRetry(uint32_t nowMs, uint32_t backoffMs)
    {
        retryAfterMs_ = nowMs + backoffMs;
        // Zero means "no deadline", so avoid that sentinel on wraparound.
        if (retryAfterMs_ == 0)
            retryAfterMs_ = 1;
    }

    void recordSuccess(uint32_t eventTime)
    {
        lastSyncedEventTime_ = eventTime;
        retryAfterMs_ = 0;
    }

    // Production completion gate: the event watermark advances only when the
    // durable writer/commit succeeded and no newer XP remains. Tests exercise
    // this same method rather than duplicating runOnce's ordering in a toy
    // wrapper. Returns true only for a fully consumed event.
    bool finishPersistenceAttempt(uint32_t eventTime, uint32_t nowMs,
                                  bool persistenceSucceeded,
                                  bool pendingXpRemains,
                                  uint32_t failureBackoffMs,
                                  uint32_t partialBackoffMs)
    {
        if (!persistenceSucceeded) {
            recordFailure(nowMs, failureBackoffMs);
            return false;
        }
        if (pendingXpRemains) {
            deferRetry(nowMs, partialBackoffMs);
            return false;
        }
        recordSuccess(eventTime);
        return true;
    }

    uint32_t lastSyncedEventTime() const { return lastSyncedEventTime_; }
    uint32_t retryAfterMs() const { return retryAfterMs_; }

  private:
    uint32_t lastSyncedEventTime_ = 0;
    uint32_t retryAfterMs_ = 0;
};
