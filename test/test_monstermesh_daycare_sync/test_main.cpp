#include "modules/monstermesh/DaycareSyncTracker.h"

#include <unity.h>

namespace
{
bool attemptSync(DaycareSyncTracker &tracker, uint32_t eventTime, uint32_t nowMs, bool persistenceSucceeded)
{
    if (!tracker.needsSync(eventTime) || !tracker.retryReady(nowMs))
        return false;
    return tracker.finishPersistenceAttempt(eventTime, nowMs,
                                            persistenceSucceeded, false,
                                            5000, 1000);
}

void test_failed_event_is_retried_then_committed_once()
{
    DaycareSyncTracker tracker;

    TEST_ASSERT_FALSE(attemptSync(tracker, 42, 1000, false));
    TEST_ASSERT_TRUE(tracker.needsSync(42));
    TEST_ASSERT_FALSE(tracker.retryReady(5999));
    TEST_ASSERT_FALSE(attemptSync(tracker, 42, 5999, true));

    TEST_ASSERT_TRUE(attemptSync(tracker, 42, 6000, true));
    TEST_ASSERT_EQUAL_UINT32(42, tracker.lastSyncedEventTime());
    TEST_ASSERT_FALSE(tracker.needsSync(42));
    TEST_ASSERT_FALSE(attemptSync(tracker, 42, 6001, true));
}

void test_new_event_after_success_is_eligible_immediately()
{
    DaycareSyncTracker tracker;
    tracker.recordSuccess(7);

    TEST_ASSERT_FALSE(tracker.needsSync(7));
    TEST_ASSERT_TRUE(tracker.needsSync(8));
    TEST_ASSERT_TRUE(tracker.retryReady(0));
}

void test_partial_success_keeps_same_event_pending()
{
    DaycareSyncTracker tracker;
    TEST_ASSERT_FALSE(tracker.finishPersistenceAttempt(9, 100, true, true,
                                                       5000, 1000));
    TEST_ASSERT_TRUE(tracker.needsSync(9));
    TEST_ASSERT_FALSE(tracker.retryReady(1099));
    TEST_ASSERT_TRUE(tracker.retryReady(1100));
    TEST_ASSERT_TRUE(tracker.finishPersistenceAttempt(9, 1100, true, false,
                                                      5000, 1000));
    TEST_ASSERT_FALSE(tracker.needsSync(9));
}
} // namespace

void setUp() {}
void tearDown() {}

void setup()
{
    UNITY_BEGIN();
    RUN_TEST(test_failed_event_is_retried_then_committed_once);
    RUN_TEST(test_new_event_after_success_is_eligible_immediately);
    RUN_TEST(test_partial_success_keeps_same_event_pending);
    exit(UNITY_END());
}

void loop() {}
