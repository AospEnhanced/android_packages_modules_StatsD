// Copyright (C) 2017 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "src/metrics/DurationMetricProducer.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <stdio.h>

#include <set>
#include <unordered_map>
#include <vector>

#include "metrics_test_helper.h"
#include "src/condition/ConditionWizard.h"
#include "src/stats_log_util.h"
#include "stats_event.h"
#include "tests/statsd_test_util.h"

using namespace android::os::statsd;
using namespace testing;
using android::sp;
using std::set;
using std::unordered_map;
using std::vector;

#ifdef __ANDROID__

namespace android {
namespace os {
namespace statsd {


namespace {

const ConfigKey kConfigKey(0, 12345);
const uint64_t protoHash = 0x1234567890;
void makeLogEvent(LogEvent* logEvent, int64_t timestampNs, int atomId) {
    AStatsEvent* statsEvent = AStatsEvent_obtain();
    AStatsEvent_setAtomId(statsEvent, atomId);
    AStatsEvent_overwriteTimestamp(statsEvent, timestampNs);

    parseStatsEventToLogEvent(statsEvent, logEvent);
}

}  // namespace

// Setup for parameterized tests.
class DurationMetricProducerTest_PartialBucket : public TestWithParam<BucketSplitEvent> {};

INSTANTIATE_TEST_SUITE_P(DurationMetricProducerTest_PartialBucket,
                         DurationMetricProducerTest_PartialBucket,
                         testing::Values(APP_UPGRADE, BOOT_COMPLETE));

TEST(DurationMetricTrackerTest, TestFirstBucket) {
    sp<MockConditionWizard> wizard = new NaggyMock<MockConditionWizard>();
    DurationMetric metric;
    metric.set_id(1);
    metric.set_bucket(ONE_MINUTE);
    metric.set_aggregation_type(DurationMetric_AggregationType_SUM);

    FieldMatcher dimensions;
    sp<MockConfigMetadataProvider> provider = makeMockConfigMetadataProvider(/*enabled=*/false);

    DurationMetricProducer durationProducer(
            kConfigKey, metric, -1 /*no condition*/, {}, -1 /*what index not needed*/,
            1 /* start index */, 2 /* stop index */, 3 /* stop_all index */, false /*nesting*/,
            wizard, protoHash, dimensions, 5, 600 * NS_PER_SEC + NS_PER_SEC / 2, provider);

    EXPECT_EQ(600500000000, durationProducer.mCurrentBucketStartTimeNs);
    EXPECT_EQ(10, durationProducer.mCurrentBucketNum);
    EXPECT_EQ(660000000005, durationProducer.getCurrentBucketEndTimeNs());
}

TEST(DurationMetricTrackerTest, TestNoCondition) {
    sp<MockConditionWizard> wizard = new NaggyMock<MockConditionWizard>();
    int64_t bucketStartTimeNs = 10000000000;
    int64_t bucketSizeNs = TimeUnitToBucketSizeInMillis(ONE_MINUTE) * 1000000LL;

    DurationMetric metric;
    metric.set_id(1);
    metric.set_bucket(ONE_MINUTE);
    metric.set_aggregation_type(DurationMetric_AggregationType_SUM);

    int tagId = 1;
    LogEvent event1(/*uid=*/0, /*pid=*/0);
    makeLogEvent(&event1, bucketStartTimeNs + 1, tagId);
    LogEvent event2(/*uid=*/0, /*pid=*/0);
    makeLogEvent(&event2, bucketStartTimeNs + bucketSizeNs + 2, tagId);

    FieldMatcher dimensions;
    sp<MockConfigMetadataProvider> provider = makeMockConfigMetadataProvider(/*enabled=*/false);

    DurationMetricProducer durationProducer(
            kConfigKey, metric, -1 /*no condition*/, {}, -1 /*what index not needed*/,
            1 /* start index */, 2 /* stop index */, 3 /* stop_all index */, false /*nesting*/,
            wizard, protoHash, dimensions, bucketStartTimeNs, bucketStartTimeNs, provider);

    durationProducer.onMatchedLogEvent(1 /* start index*/, event1);
    durationProducer.onMatchedLogEvent(2 /* stop index*/, event2);
    durationProducer.flushIfNeededLocked(bucketStartTimeNs + 2 * bucketSizeNs + 1);
    ASSERT_EQ(1UL, durationProducer.mPastBuckets.size());
    EXPECT_TRUE(durationProducer.mPastBuckets.find(DEFAULT_METRIC_DIMENSION_KEY) !=
                durationProducer.mPastBuckets.end());
    const auto& buckets = durationProducer.mPastBuckets[DEFAULT_METRIC_DIMENSION_KEY];
    ASSERT_EQ(2UL, buckets.size());
    EXPECT_EQ(bucketStartTimeNs, buckets[0].mBucketStartNs);
    EXPECT_EQ(bucketStartTimeNs + bucketSizeNs, buckets[0].mBucketEndNs);
    EXPECT_EQ(bucketSizeNs - 1LL, buckets[0].mDuration);
    EXPECT_EQ(bucketStartTimeNs + bucketSizeNs, buckets[1].mBucketStartNs);
    EXPECT_EQ(bucketStartTimeNs + 2 * bucketSizeNs, buckets[1].mBucketEndNs);
    EXPECT_EQ(2LL, buckets[1].mDuration);
}

TEST(DurationMetricTrackerTest, TestNonSlicedCondition) {
    sp<MockConditionWizard> wizard = new NaggyMock<MockConditionWizard>();
    int64_t bucketStartTimeNs = 10000000000;
    int64_t bucketSizeNs = TimeUnitToBucketSizeInMillis(ONE_MINUTE) * 1000000LL;

    DurationMetric metric;
    metric.set_id(1);
    metric.set_bucket(ONE_MINUTE);
    metric.set_aggregation_type(DurationMetric_AggregationType_SUM);

    int tagId = 1;
    LogEvent event1(/*uid=*/0, /*pid=*/0);
    makeLogEvent(&event1, bucketStartTimeNs + 1, tagId);
    LogEvent event2(/*uid=*/0, /*pid=*/0);
    makeLogEvent(&event2, bucketStartTimeNs + 2, tagId);
    LogEvent event3(/*uid=*/0, /*pid=*/0);
    makeLogEvent(&event3, bucketStartTimeNs + bucketSizeNs + 1, tagId);
    LogEvent event4(/*uid=*/0, /*pid=*/0);
    makeLogEvent(&event4, bucketStartTimeNs + bucketSizeNs + 3, tagId);

    FieldMatcher dimensions;
    sp<MockConfigMetadataProvider> provider = makeMockConfigMetadataProvider(/*enabled=*/false);

    DurationMetricProducer durationProducer(
            kConfigKey, metric, 0 /* condition index */, {ConditionState::kUnknown},
            -1 /*what index not needed*/, 1 /* start index */, 2 /* stop index */,
            3 /* stop_all index */, false /*nesting*/, wizard, protoHash, dimensions,
            bucketStartTimeNs, bucketStartTimeNs, provider);
    durationProducer.mCondition = ConditionState::kFalse;

    assertConditionTimer(durationProducer.mConditionTimer, false, 0, 0);
    EXPECT_FALSE(durationProducer.mCondition);
    EXPECT_FALSE(durationProducer.isConditionSliced());

    durationProducer.onMatchedLogEvent(1 /* start index*/, event1);
    durationProducer.onMatchedLogEvent(2 /* stop index*/, event2);
    durationProducer.flushIfNeededLocked(bucketStartTimeNs + bucketSizeNs + 1);
    ASSERT_EQ(0UL, durationProducer.mPastBuckets.size());

    int64_t conditionStartTimeNs = bucketStartTimeNs + bucketSizeNs + 2;
    int64_t bucket2EndTimeNs = bucketStartTimeNs + 2 * bucketSizeNs;
    durationProducer.onMatchedLogEvent(1 /* start index*/, event3);
    durationProducer.onConditionChanged(true /* condition */, conditionStartTimeNs);
    assertConditionTimer(durationProducer.mConditionTimer, true, 0, conditionStartTimeNs);
    durationProducer.onMatchedLogEvent(2 /* stop index*/, event4);
    durationProducer.flushIfNeededLocked(bucket2EndTimeNs + 1);
    assertConditionTimer(durationProducer.mConditionTimer, true, 0, bucket2EndTimeNs,
                         /*currentBucketStartDelayNs=*/1);
    ASSERT_EQ(1UL, durationProducer.mPastBuckets.size());
    EXPECT_TRUE(durationProducer.mPastBuckets.find(DEFAULT_METRIC_DIMENSION_KEY) !=
                durationProducer.mPastBuckets.end());
    const auto& buckets2 = durationProducer.mPastBuckets[DEFAULT_METRIC_DIMENSION_KEY];
    ASSERT_EQ(1UL, buckets2.size());
    EXPECT_EQ(bucketStartTimeNs + bucketSizeNs, buckets2[0].mBucketStartNs);
    EXPECT_EQ(bucket2EndTimeNs, buckets2[0].mBucketEndNs);
    EXPECT_EQ(1LL, buckets2[0].mDuration);
    EXPECT_EQ(bucket2EndTimeNs - conditionStartTimeNs, buckets2[0].mConditionTrueNs);
}

TEST(DurationMetricTrackerTest, TestNonSlicedConditionUnknownState) {
    sp<MockConditionWizard> wizard = new NaggyMock<MockConditionWizard>();
    int64_t bucketStartTimeNs = 10000000000;
    int64_t bucketSizeNs = TimeUnitToBucketSizeInMillis(ONE_MINUTE) * 1000000LL;

    DurationMetric metric;
    metric.set_id(1);
    metric.set_bucket(ONE_MINUTE);
    metric.set_aggregation_type(DurationMetric_AggregationType_SUM);

    int tagId = 1;
    LogEvent event1(/*uid=*/0, /*pid=*/0);
    makeLogEvent(&event1, bucketStartTimeNs + 1, tagId);
    LogEvent event2(/*uid=*/0, /*pid=*/0);
    makeLogEvent(&event2, bucketStartTimeNs + 2, tagId);
    LogEvent event3(/*uid=*/0, /*pid=*/0);
    makeLogEvent(&event3, bucketStartTimeNs + bucketSizeNs + 1, tagId);
    LogEvent event4(/*uid=*/0, /*pid=*/0);
    makeLogEvent(&event4, bucketStartTimeNs + bucketSizeNs + 3, tagId);

    FieldMatcher dimensions;
    sp<MockConfigMetadataProvider> provider = makeMockConfigMetadataProvider(/*enabled=*/false);

    DurationMetricProducer durationProducer(
            kConfigKey, metric, 0 /* condition index */, {ConditionState::kUnknown},
            -1 /*what index not needed*/, 1 /* start index */, 2 /* stop index */,
            3 /* stop_all index */, false /*nesting*/, wizard, protoHash, dimensions,
            bucketStartTimeNs, bucketStartTimeNs, provider);

    EXPECT_EQ(ConditionState::kUnknown, durationProducer.mCondition);
    EXPECT_FALSE(durationProducer.isConditionSliced());

    durationProducer.onMatchedLogEvent(1 /* start index*/, event1);
    durationProducer.onMatchedLogEvent(2 /* stop index*/, event2);
    durationProducer.flushIfNeededLocked(bucketStartTimeNs + bucketSizeNs + 1);
    ASSERT_EQ(0UL, durationProducer.mPastBuckets.size());

    durationProducer.onMatchedLogEvent(1 /* start index*/, event3);
    durationProducer.onConditionChanged(true /* condition */, bucketStartTimeNs + bucketSizeNs + 2);
    durationProducer.onMatchedLogEvent(2 /* stop index*/, event4);
    durationProducer.flushIfNeededLocked(bucketStartTimeNs + 2 * bucketSizeNs + 1);
    ASSERT_EQ(1UL, durationProducer.mPastBuckets.size());
    const auto& buckets2 = durationProducer.mPastBuckets[DEFAULT_METRIC_DIMENSION_KEY];
    ASSERT_EQ(1UL, buckets2.size());
    EXPECT_EQ(bucketStartTimeNs + bucketSizeNs, buckets2[0].mBucketStartNs);
    EXPECT_EQ(bucketStartTimeNs + 2 * bucketSizeNs, buckets2[0].mBucketEndNs);
    EXPECT_EQ(1LL, buckets2[0].mDuration);
}

TEST_P(DurationMetricProducerTest_PartialBucket, TestSumDuration) {
    /**
     * The duration starts from the first bucket, through the two partial buckets (10-70sec),
     * another bucket, and ends at the beginning of the next full bucket.
     * Expected buckets:
     *  - [10,25]: 14 secs
     *  - [25,70]: All 45 secs
     *  - [70,130]: All 60 secs
     *  - [130, 210]: Only 5 secs (event ended at 135sec)
     */
    int64_t bucketStartTimeNs = 10000000000;
    int64_t bucketSizeNs = TimeUnitToBucketSizeInMillis(ONE_MINUTE) * 1000000LL;
    int tagId = 1;

    DurationMetric metric;
    metric.set_id(1);
    metric.set_bucket(ONE_MINUTE);
    metric.set_aggregation_type(DurationMetric_AggregationType_SUM);
    metric.set_split_bucket_for_app_upgrade(true);
    sp<MockConditionWizard> wizard = new NaggyMock<MockConditionWizard>();
    FieldMatcher dimensions;
    sp<MockConfigMetadataProvider> provider = makeMockConfigMetadataProvider(/*enabled=*/false);

    DurationMetricProducer durationProducer(
            kConfigKey, metric, -1 /* no condition */, {}, -1 /*what index not needed*/,
            1 /* start index */, 2 /* stop index */, 3 /* stop_all index */, false /*nesting*/,
            wizard, protoHash, dimensions, bucketStartTimeNs, bucketStartTimeNs, provider);

    int64_t startTimeNs = bucketStartTimeNs + 1 * NS_PER_SEC;
    LogEvent event1(/*uid=*/0, /*pid=*/0);
    makeLogEvent(&event1, startTimeNs, tagId);
    durationProducer.onMatchedLogEvent(1 /* start index*/, event1);
    ASSERT_EQ(0UL, durationProducer.mPastBuckets.size());
    EXPECT_EQ(bucketStartTimeNs, durationProducer.mCurrentBucketStartTimeNs);

    int64_t partialBucketSplitTimeNs = bucketStartTimeNs + 15 * NS_PER_SEC;
    switch (GetParam()) {
        case APP_UPGRADE:
            durationProducer.notifyAppUpgrade(partialBucketSplitTimeNs);
            break;
        case BOOT_COMPLETE:
            durationProducer.onStatsdInitCompleted(partialBucketSplitTimeNs);
            break;
    }
    ASSERT_EQ(1UL, durationProducer.mPastBuckets[DEFAULT_METRIC_DIMENSION_KEY].size());
    std::vector<DurationBucket> buckets =
            durationProducer.mPastBuckets[DEFAULT_METRIC_DIMENSION_KEY];
    EXPECT_EQ(bucketStartTimeNs, buckets[0].mBucketStartNs);
    EXPECT_EQ(partialBucketSplitTimeNs, buckets[0].mBucketEndNs);
    EXPECT_EQ(partialBucketSplitTimeNs - startTimeNs, buckets[0].mDuration);
    EXPECT_EQ(partialBucketSplitTimeNs, durationProducer.mCurrentBucketStartTimeNs);
    EXPECT_EQ(0, durationProducer.getCurrentBucketNum());

    // We skip ahead one bucket, so we fill in the first two partial buckets and one full bucket.
    int64_t endTimeNs = startTimeNs + 125 * NS_PER_SEC;
    LogEvent event2(/*uid=*/0, /*pid=*/0);
    makeLogEvent(&event2, endTimeNs, tagId);
    durationProducer.onMatchedLogEvent(2 /* stop index*/, event2);
    buckets = durationProducer.mPastBuckets[DEFAULT_METRIC_DIMENSION_KEY];
    ASSERT_EQ(3UL, buckets.size());
    EXPECT_EQ(partialBucketSplitTimeNs, buckets[1].mBucketStartNs);
    EXPECT_EQ(bucketStartTimeNs + bucketSizeNs, buckets[1].mBucketEndNs);
    EXPECT_EQ(bucketStartTimeNs + bucketSizeNs - partialBucketSplitTimeNs, buckets[1].mDuration);
    EXPECT_EQ(bucketStartTimeNs + bucketSizeNs, buckets[2].mBucketStartNs);
    EXPECT_EQ(bucketStartTimeNs + 2 * bucketSizeNs, buckets[2].mBucketEndNs);
    EXPECT_EQ(bucketSizeNs, buckets[2].mDuration);
}

TEST_P(DurationMetricProducerTest_PartialBucket, TestSumDurationWithSplitInFollowingBucket) {
    /**
     * Expected buckets (start at 11s, upgrade at 75s, end at 135s):
     *  - [10,70]: 59 secs
     *  - [70,75]: 5 sec
     *  - [75,130]: 55 secs
     */
    int64_t bucketStartTimeNs = 10000000000;
    int64_t bucketSizeNs = TimeUnitToBucketSizeInMillis(ONE_MINUTE) * 1000000LL;
    int tagId = 1;

    DurationMetric metric;
    metric.set_id(1);
    metric.set_bucket(ONE_MINUTE);
    metric.set_aggregation_type(DurationMetric_AggregationType_SUM);
    metric.set_split_bucket_for_app_upgrade(true);
    sp<MockConditionWizard> wizard = new NaggyMock<MockConditionWizard>();
    FieldMatcher dimensions;
    sp<MockConfigMetadataProvider> provider = makeMockConfigMetadataProvider(/*enabled=*/false);

    DurationMetricProducer durationProducer(
            kConfigKey, metric, -1 /* no condition */, {}, -1 /*what index not needed*/,
            1 /* start index */, 2 /* stop index */, 3 /* stop_all index */, false /*nesting*/,
            wizard, protoHash, dimensions, bucketStartTimeNs, bucketStartTimeNs, provider);

    int64_t startTimeNs = bucketStartTimeNs + 1 * NS_PER_SEC;
    LogEvent event1(/*uid=*/0, /*pid=*/0);
    makeLogEvent(&event1, startTimeNs, tagId);
    durationProducer.onMatchedLogEvent(1 /* start index*/, event1);
    ASSERT_EQ(0UL, durationProducer.mPastBuckets.size());
    EXPECT_EQ(bucketStartTimeNs, durationProducer.mCurrentBucketStartTimeNs);

    int64_t partialBucketSplitTimeNs = bucketStartTimeNs + 65 * NS_PER_SEC;
    switch (GetParam()) {
        case APP_UPGRADE:
            durationProducer.notifyAppUpgrade(partialBucketSplitTimeNs);
            break;
        case BOOT_COMPLETE:
            durationProducer.onStatsdInitCompleted(partialBucketSplitTimeNs);
            break;
    }
    ASSERT_EQ(2UL, durationProducer.mPastBuckets[DEFAULT_METRIC_DIMENSION_KEY].size());
    std::vector<DurationBucket> buckets =
            durationProducer.mPastBuckets[DEFAULT_METRIC_DIMENSION_KEY];
    EXPECT_EQ(bucketStartTimeNs, buckets[0].mBucketStartNs);
    EXPECT_EQ(bucketStartTimeNs + bucketSizeNs, buckets[0].mBucketEndNs);
    EXPECT_EQ(bucketStartTimeNs + bucketSizeNs - startTimeNs, buckets[0].mDuration);
    EXPECT_EQ(bucketStartTimeNs + bucketSizeNs, buckets[1].mBucketStartNs);
    EXPECT_EQ(partialBucketSplitTimeNs, buckets[1].mBucketEndNs);
    EXPECT_EQ(partialBucketSplitTimeNs - (bucketStartTimeNs + bucketSizeNs), buckets[1].mDuration);
    EXPECT_EQ(partialBucketSplitTimeNs, durationProducer.mCurrentBucketStartTimeNs);
    EXPECT_EQ(1, durationProducer.getCurrentBucketNum());

    // We skip ahead one bucket, so we fill in the first two partial buckets and one full bucket.
    int64_t endTimeNs = startTimeNs + 125 * NS_PER_SEC;
    LogEvent event2(/*uid=*/0, /*pid=*/0);
    makeLogEvent(&event2, endTimeNs, tagId);
    durationProducer.onMatchedLogEvent(2 /* stop index*/, event2);
    buckets = durationProducer.mPastBuckets[DEFAULT_METRIC_DIMENSION_KEY];
    ASSERT_EQ(3UL, buckets.size());
    EXPECT_EQ(partialBucketSplitTimeNs, buckets[2].mBucketStartNs);
    EXPECT_EQ(bucketStartTimeNs + 2 * bucketSizeNs, buckets[2].mBucketEndNs);
    EXPECT_EQ(bucketStartTimeNs + 2 * bucketSizeNs - partialBucketSplitTimeNs,
              buckets[2].mDuration);
}

TEST_P(DurationMetricProducerTest_PartialBucket, TestSumDurationAnomaly) {
    sp<AlarmMonitor> alarmMonitor;
    int64_t bucketStartTimeNs = 10000000000;
    int64_t bucketSizeNs = TimeUnitToBucketSizeInMillis(ONE_MINUTE) * 1000000LL;
    int tagId = 1;

    // Setup metric with alert.
    DurationMetric metric;
    metric.set_id(1);
    metric.set_bucket(ONE_MINUTE);
    metric.set_aggregation_type(DurationMetric_AggregationType_SUM);
    metric.set_split_bucket_for_app_upgrade(true);
    Alert alert;
    alert.set_num_buckets(3);
    alert.set_trigger_if_sum_gt(2);

    sp<MockConditionWizard> wizard = new NaggyMock<MockConditionWizard>();
    FieldMatcher dimensions;
    sp<MockConfigMetadataProvider> provider = makeMockConfigMetadataProvider(/*enabled=*/false);

    DurationMetricProducer durationProducer(
            kConfigKey, metric, -1 /* no condition */, {}, -1 /*what index not needed*/,
            1 /* start index */, 2 /* stop index */, 3 /* stop_all index */, false /*nesting*/,
            wizard, protoHash, dimensions, bucketStartTimeNs, bucketStartTimeNs, provider);

    sp<AnomalyTracker> anomalyTracker =
            durationProducer.addAnomalyTracker(alert, alarmMonitor, UPDATE_NEW, bucketStartTimeNs);
    EXPECT_TRUE(anomalyTracker != nullptr);

    int64_t startTimeNs = bucketStartTimeNs + 1;
    LogEvent event1(/*uid=*/0, /*pid=*/0);
    makeLogEvent(&event1, startTimeNs, tagId);
    durationProducer.onMatchedLogEvent(1 /* start index*/, event1);

    int64_t partialBucketSplitTimeNs = bucketStartTimeNs + 15 * NS_PER_SEC;
    switch (GetParam()) {
        case APP_UPGRADE:
            durationProducer.notifyAppUpgrade(partialBucketSplitTimeNs);
            break;
        case BOOT_COMPLETE:
            durationProducer.onStatsdInitCompleted(partialBucketSplitTimeNs);
            break;
    }

    // We skip ahead one bucket, so we fill in the first two partial buckets and one full bucket.
    int64_t endTimeNs = startTimeNs + 65 * NS_PER_SEC;
    LogEvent event2(/*uid=*/0, /*pid=*/0);
    makeLogEvent(&event2, endTimeNs, tagId);
    durationProducer.onMatchedLogEvent(2 /* stop index*/, event2);

    EXPECT_EQ(bucketStartTimeNs + bucketSizeNs - startTimeNs,
              anomalyTracker->getSumOverPastBuckets(DEFAULT_METRIC_DIMENSION_KEY));
}

TEST_P(DurationMetricProducerTest_PartialBucket, TestMaxDuration) {
    int64_t bucketStartTimeNs = 10000000000;
    int64_t bucketSizeNs = TimeUnitToBucketSizeInMillis(ONE_MINUTE) * 1000000LL;
    int tagId = 1;

    DurationMetric metric;
    metric.set_id(1);
    metric.set_bucket(ONE_MINUTE);
    metric.set_aggregation_type(DurationMetric_AggregationType_MAX_SPARSE);
    metric.set_split_bucket_for_app_upgrade(true);

    sp<MockConditionWizard> wizard = new NaggyMock<MockConditionWizard>();
    FieldMatcher dimensions;
    sp<MockConfigMetadataProvider> provider = makeMockConfigMetadataProvider(/*enabled=*/false);

    DurationMetricProducer durationProducer(
            kConfigKey, metric, -1 /* no condition */, {}, -1 /*what index not needed*/,
            1 /* start index */, 2 /* stop index */, 3 /* stop_all index */, false /*nesting*/,
            wizard, protoHash, dimensions, bucketStartTimeNs, bucketStartTimeNs, provider);

    int64_t startTimeNs = bucketStartTimeNs + 1;
    LogEvent event1(/*uid=*/0, /*pid=*/0);
    makeLogEvent(&event1, startTimeNs, tagId);
    durationProducer.onMatchedLogEvent(1 /* start index*/, event1);
    ASSERT_EQ(0UL, durationProducer.mPastBuckets.size());
    EXPECT_EQ(bucketStartTimeNs, durationProducer.mCurrentBucketStartTimeNs);

    int64_t partialBucketSplitTimeNs = bucketStartTimeNs + 15 * NS_PER_SEC;
    switch (GetParam()) {
        case APP_UPGRADE:
            durationProducer.notifyAppUpgrade(partialBucketSplitTimeNs);
            break;
        case BOOT_COMPLETE:
            durationProducer.onStatsdInitCompleted(partialBucketSplitTimeNs);
            break;
    }
    ASSERT_EQ(0UL, durationProducer.mPastBuckets[DEFAULT_METRIC_DIMENSION_KEY].size());
    EXPECT_EQ(partialBucketSplitTimeNs, durationProducer.mCurrentBucketStartTimeNs);
    EXPECT_EQ(0, durationProducer.getCurrentBucketNum());

    // We skip ahead one bucket, so we fill in the first two partial buckets and one full bucket.
    int64_t endTimeNs = startTimeNs + 125 * NS_PER_SEC;
    LogEvent event2(/*uid=*/0, /*pid=*/0);
    makeLogEvent(&event2, endTimeNs, tagId);
    durationProducer.onMatchedLogEvent(2 /* stop index*/, event2);
    ASSERT_EQ(0UL, durationProducer.mPastBuckets[DEFAULT_METRIC_DIMENSION_KEY].size());

    durationProducer.flushIfNeededLocked(bucketStartTimeNs + 3 * bucketSizeNs + 1);
    std::vector<DurationBucket> buckets =
            durationProducer.mPastBuckets[DEFAULT_METRIC_DIMENSION_KEY];
    ASSERT_EQ(1UL, buckets.size());
    EXPECT_EQ(bucketStartTimeNs + 2 * bucketSizeNs, buckets[0].mBucketStartNs);
    EXPECT_EQ(bucketStartTimeNs + 3 * bucketSizeNs, buckets[0].mBucketEndNs);
    EXPECT_EQ(endTimeNs - startTimeNs, buckets[0].mDuration);
}

TEST_P(DurationMetricProducerTest_PartialBucket, TestMaxDurationWithSplitInNextBucket) {
    int64_t bucketStartTimeNs = 10000000000;
    int64_t bucketSizeNs = TimeUnitToBucketSizeInMillis(ONE_MINUTE) * 1000000LL;
    int tagId = 1;

    DurationMetric metric;
    metric.set_id(1);
    metric.set_bucket(ONE_MINUTE);
    metric.set_aggregation_type(DurationMetric_AggregationType_MAX_SPARSE);
    metric.set_split_bucket_for_app_upgrade(true);

    sp<MockConditionWizard> wizard = new NaggyMock<MockConditionWizard>();
    FieldMatcher dimensions;
    sp<MockConfigMetadataProvider> provider = makeMockConfigMetadataProvider(/*enabled=*/false);

    DurationMetricProducer durationProducer(
            kConfigKey, metric, -1 /* no condition */, {}, -1 /*what index not needed*/,
            1 /* start index */, 2 /* stop index */, 3 /* stop_all index */, false /*nesting*/,
            wizard, protoHash, dimensions, bucketStartTimeNs, bucketStartTimeNs, provider);

    int64_t startTimeNs = bucketStartTimeNs + 1;
    LogEvent event1(/*uid=*/0, /*pid=*/0);
    makeLogEvent(&event1, startTimeNs, tagId);
    durationProducer.onMatchedLogEvent(1 /* start index*/, event1);
    ASSERT_EQ(0UL, durationProducer.mPastBuckets.size());
    EXPECT_EQ(bucketStartTimeNs, durationProducer.mCurrentBucketStartTimeNs);

    int64_t partialBucketSplitTimeNs = bucketStartTimeNs + 65 * NS_PER_SEC;
    switch (GetParam()) {
        case APP_UPGRADE:
            durationProducer.notifyAppUpgrade(partialBucketSplitTimeNs);
            break;
        case BOOT_COMPLETE:
            durationProducer.onStatsdInitCompleted(partialBucketSplitTimeNs);
            break;
    }
    ASSERT_EQ(0UL, durationProducer.mPastBuckets[DEFAULT_METRIC_DIMENSION_KEY].size());
    EXPECT_EQ(partialBucketSplitTimeNs, durationProducer.mCurrentBucketStartTimeNs);
    EXPECT_EQ(1, durationProducer.getCurrentBucketNum());

    // Stop occurs in the same partial bucket as created for the app upgrade.
    int64_t endTimeNs = startTimeNs + 115 * NS_PER_SEC;
    LogEvent event2(/*uid=*/0, /*pid=*/0);
    makeLogEvent(&event2, endTimeNs, tagId);
    durationProducer.onMatchedLogEvent(2 /* stop index*/, event2);
    ASSERT_EQ(0UL, durationProducer.mPastBuckets[DEFAULT_METRIC_DIMENSION_KEY].size());
    EXPECT_EQ(partialBucketSplitTimeNs, durationProducer.mCurrentBucketStartTimeNs);

    durationProducer.flushIfNeededLocked(bucketStartTimeNs + 2 * bucketSizeNs + 1);
    std::vector<DurationBucket> buckets =
            durationProducer.mPastBuckets[DEFAULT_METRIC_DIMENSION_KEY];
    ASSERT_EQ(1UL, buckets.size());
    EXPECT_EQ(partialBucketSplitTimeNs, buckets[0].mBucketStartNs);
    EXPECT_EQ(bucketStartTimeNs + 2 * bucketSizeNs, buckets[0].mBucketEndNs);
    EXPECT_EQ(endTimeNs - startTimeNs, buckets[0].mDuration);
}

TEST(DurationMetricProducerTest, TestSumDurationAppUpgradeSplitDisabled) {
    /**
     * The duration starts from the first bucket, through one full bucket (10-70sec).
     * The app upgrade should not split a partial bucket.
     * Expected buckets:
     *  - [10,70]: All 60 secs
     *  - [70, 75]: Only 5 secs (event ended at 75sec)
     */
    int64_t bucketStartTimeNs = 10000000000;
    int64_t bucketSizeNs = TimeUnitToBucketSizeInMillis(ONE_MINUTE) * 1000000LL;
    int tagId = 1;

    DurationMetric metric;
    metric.set_id(1);
    metric.set_bucket(ONE_MINUTE);
    metric.set_aggregation_type(DurationMetric_AggregationType_SUM);
    metric.set_split_bucket_for_app_upgrade(false);
    sp<MockConditionWizard> wizard = new NaggyMock<MockConditionWizard>();
    FieldMatcher dimensions;
    sp<MockConfigMetadataProvider> provider = makeMockConfigMetadataProvider(/*enabled=*/false);

    DurationMetricProducer durationProducer(
            kConfigKey, metric, -1 /* no condition */, {}, -1 /*what index not needed*/,
            1 /* start index */, 2 /* stop index */, 3 /* stop_all index */, false /*nesting*/,
            wizard, protoHash, dimensions, bucketStartTimeNs, bucketStartTimeNs, provider);

    int64_t startTimeNs = bucketStartTimeNs + 1 * NS_PER_SEC;
    LogEvent event1(/*uid=*/0, /*pid=*/0);
    makeLogEvent(&event1, startTimeNs, tagId);
    durationProducer.onMatchedLogEvent(1 /* start index*/, event1);
    ASSERT_EQ(0UL, durationProducer.mPastBuckets.size());
    EXPECT_EQ(bucketStartTimeNs, durationProducer.mCurrentBucketStartTimeNs);

    int64_t appUpgradeTimeNs = bucketStartTimeNs + 15 * NS_PER_SEC;
    durationProducer.notifyAppUpgrade(appUpgradeTimeNs);

    ASSERT_EQ(0UL, durationProducer.mPastBuckets.size());
    EXPECT_EQ(0, durationProducer.getCurrentBucketNum());

    // We skip ahead one bucket, so we fill in one full bucket and expect 0 partial buckets.
    int64_t endTimeNs = startTimeNs + 65 * NS_PER_SEC;
    LogEvent event2(/*uid=*/0, /*pid=*/0);
    makeLogEvent(&event2, endTimeNs, tagId);
    durationProducer.onMatchedLogEvent(2 /* stop index*/, event2);
    ASSERT_EQ(1UL, durationProducer.mPastBuckets[DEFAULT_METRIC_DIMENSION_KEY].size());
    DurationBucket bucket = durationProducer.mPastBuckets[DEFAULT_METRIC_DIMENSION_KEY][0];
    EXPECT_EQ(bucketStartTimeNs, bucket.mBucketStartNs);
    EXPECT_EQ(bucketStartTimeNs + bucketSizeNs, bucket.mBucketEndNs);
    EXPECT_EQ(bucketSizeNs - 1 * NS_PER_SEC, bucket.mDuration);
    EXPECT_EQ(1, durationProducer.getCurrentBucketNum());
}

TEST(DurationMetricProducerTest, TestClearCurrentSlicedTrackerMapWhenStop) {
    int64_t bucketStartTimeNs = 10000000000;
    int64_t bucketSizeNs = TimeUnitToBucketSizeInMillis(ONE_MINUTE) * 1000000LL;
    int tagId = 1;

    DurationMetric metric;
    metric.set_id(1);
    metric.set_bucket(ONE_MINUTE);
    metric.set_aggregation_type(DurationMetric_AggregationType_SUM);
    sp<MockConditionWizard> wizard = new NaggyMock<MockConditionWizard>();
    FieldMatcher dimensions;
    sp<MockConfigMetadataProvider> provider = makeMockConfigMetadataProvider(/*enabled=*/false);

    LogEvent event1(/*uid=*/0, /*pid=*/0);
    makeLogEvent(&event1, bucketStartTimeNs + 50, tagId);
    LogEvent event2(/*uid=*/0, /*pid=*/0);
    makeLogEvent(&event2, bucketStartTimeNs + 100, tagId);
    LogEvent event3(/*uid=*/0, /*pid=*/0);
    makeLogEvent(&event3, bucketStartTimeNs + 150, tagId);
    LogEvent event4(/*uid=*/0, /*pid=*/0);
    makeLogEvent(&event4, bucketStartTimeNs + bucketSizeNs + 5, tagId);

    DurationMetricProducer durationProducer(
            kConfigKey, metric, 0 /* condition index */, {ConditionState::kUnknown},
            -1 /*what index not needed*/, 1 /* start index */, 2 /* stop index */,
            3 /* stop_all index */, false /*nesting*/, wizard, protoHash, dimensions,
            bucketStartTimeNs, bucketStartTimeNs, provider);

    durationProducer.onConditionChanged(true /* condition */, bucketStartTimeNs + 5);
    durationProducer.onMatchedLogEvent(1 /* start index*/, event1);
    durationProducer.onMatchedLogEvent(2 /* stop index*/, event2);
    durationProducer.onMatchedLogEvent(1 /* start index*/, event3);
    durationProducer.onConditionChanged(false /* condition */, bucketStartTimeNs + 200);
    durationProducer.flushIfNeededLocked(bucketStartTimeNs + bucketSizeNs + 1);
    durationProducer.onMatchedLogEvent(2 /* stop index*/, event4);

    ASSERT_TRUE(durationProducer.mCurrentSlicedDurationTrackerMap.empty());
    EXPECT_EQ(1UL, durationProducer.mPastBuckets.size());
    EXPECT_EQ(1, durationProducer.getCurrentBucketNum());
}

}  // namespace statsd
}  // namespace os
}  // namespace android
#else
GTEST_LOG_(INFO) << "This test does nothing.\n";
#endif
