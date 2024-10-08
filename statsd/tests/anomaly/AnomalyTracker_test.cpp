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

#include "src/anomaly/AnomalyTracker.h"

#include <gtest/gtest.h>
#include <math.h>
#include <stdio.h>

#include <vector>

#include "src/subscriber/SubscriberReporter.h"
#include "tests/statsd_test_util.h"

using namespace testing;
using android::sp;
using ::ndk::SharedRefBase;
using std::set;
using std::unordered_map;
using std::vector;

#ifdef __ANDROID__

namespace android {
namespace os {
namespace statsd {

namespace {
const int kConfigUid = 0;
const int kConfigId = 12345;
const ConfigKey kConfigKey(kConfigUid, kConfigId);
}  // anonymous namespace

MetricDimensionKey getMockMetricDimensionKey(int key, string value) {
    int pos[] = {key, 0, 0};
    HashableDimensionKey dim;
    dim.addValue(FieldValue(Field(1, pos, 0), Value(value)));
    return MetricDimensionKey(dim, DEFAULT_DIMENSION_KEY);
}

void AddValueToBucket(const std::vector<std::pair<MetricDimensionKey, long>>& key_value_pair_list,
                      std::shared_ptr<DimToValMap> bucket) {
    for (auto itr = key_value_pair_list.begin(); itr != key_value_pair_list.end(); itr++) {
        (*bucket)[itr->first] += itr->second;
    }
}

std::shared_ptr<DimToValMap> MockBucket(
        const std::vector<std::pair<MetricDimensionKey, long>>& key_value_pair_list) {
    std::shared_ptr<DimToValMap> bucket = std::make_shared<DimToValMap>();
    AddValueToBucket(key_value_pair_list, bucket);
    return bucket;
}

// Returns the value, for the given key, in that bucket, or 0 if not present.
int64_t getBucketValue(const std::shared_ptr<DimToValMap>& bucket,
                       const MetricDimensionKey& key) {
    const auto& itr = bucket->find(key);
    if (itr != bucket->end()) {
        return itr->second;
    }
    return 0;
}

// Returns true if keys in trueList are detected as anomalies and keys in falseList are not.
bool detectAnomaliesPass(AnomalyTracker& tracker, int64_t bucketNum,
                         const std::shared_ptr<DimToValMap>& currentBucket,
                         const std::set<const MetricDimensionKey>& trueList,
                         const std::set<const MetricDimensionKey>& falseList) {
    for (const MetricDimensionKey& key : trueList) {
        if (!tracker.detectAnomaly(bucketNum, key, getBucketValue(currentBucket, key))) {
            return false;
        }
    }
    for (const MetricDimensionKey& key : falseList) {
        if (tracker.detectAnomaly(bucketNum, key, getBucketValue(currentBucket, key))) {
            return false;
        }
    }
    return true;
}

// Calls tracker.detectAndDeclareAnomaly on each key in the bucket.
void detectAndDeclareAnomalies(AnomalyTracker& tracker, int64_t bucketNum,
                               const std::shared_ptr<DimToValMap>& bucket, int64_t eventTimestamp) {
    for (const auto& kv : *bucket) {
        tracker.detectAndDeclareAnomaly(eventTimestamp, bucketNum, 0 /*metric_id*/, kv.first,
                                        kv.second);
    }
}

// Asserts that the refractory time for each key in timestamps is the corresponding
// timestamp (in ns) + refractoryPeriodSec.
// If a timestamp value is negative, instead asserts that the refractory period is inapplicable
// (either non-existant or already past).
void checkRefractoryTimes(AnomalyTracker& tracker, int64_t currTimestampNs,
                          int32_t refractoryPeriodSec,
                          const std::unordered_map<MetricDimensionKey, int64_t>& timestamps) {
    for (const auto& kv : timestamps) {
        if (kv.second < 0) {
            // Make sure that, if there is a refractory period, it is already past.
            EXPECT_LT(tracker.getRefractoryPeriodEndsSec(kv.first) * NS_PER_SEC,
                    (uint64_t)currTimestampNs)
                    << "Failure was at currTimestampNs " << currTimestampNs;
        } else {
            EXPECT_EQ(tracker.getRefractoryPeriodEndsSec(kv.first),
                      std::ceil(1.0 * kv.second / NS_PER_SEC) + refractoryPeriodSec)
                      << "Failure was at currTimestampNs " << currTimestampNs;
        }
    }
}

TEST(AnomalyTrackerTest, TestConsecutiveBuckets) {
    const int64_t bucketSizeNs = 30 * NS_PER_SEC;
    const int32_t refractoryPeriodSec = 2 * bucketSizeNs / NS_PER_SEC;
    Alert alert;
    alert.set_num_buckets(3);
    alert.set_refractory_period_secs(refractoryPeriodSec);
    alert.set_trigger_if_sum_gt(2);

    AnomalyTracker anomalyTracker(alert, kConfigKey);
    MetricDimensionKey keyA = getMockMetricDimensionKey(1, "a");
    MetricDimensionKey keyB = getMockMetricDimensionKey(1, "b");
    MetricDimensionKey keyC = getMockMetricDimensionKey(1, "c");

    int64_t eventTimestamp0 = 10 * NS_PER_SEC;
    int64_t eventTimestamp1 = bucketSizeNs + 11 * NS_PER_SEC;
    int64_t eventTimestamp2 = 2 * bucketSizeNs + 12 * NS_PER_SEC;
    int64_t eventTimestamp3 = 3 * bucketSizeNs + 13 * NS_PER_SEC;
    int64_t eventTimestamp4 = 4 * bucketSizeNs + 14 * NS_PER_SEC;
    int64_t eventTimestamp5 = 5 * bucketSizeNs + 5 * NS_PER_SEC;
    int64_t eventTimestamp6 = 6 * bucketSizeNs + 16 * NS_PER_SEC;

    std::shared_ptr<DimToValMap> bucket0 = MockBucket({{keyA, 1}, {keyB, 2}, {keyC, 1}});
    std::shared_ptr<DimToValMap> bucket1 = MockBucket({{keyA, 1}});
    std::shared_ptr<DimToValMap> bucket2 = MockBucket({{keyB, 1}});
    std::shared_ptr<DimToValMap> bucket3 = MockBucket({{keyA, 2}});
    std::shared_ptr<DimToValMap> bucket4 = MockBucket({{keyB, 5}});
    std::shared_ptr<DimToValMap> bucket5 = MockBucket({{keyA, 2}});
    std::shared_ptr<DimToValMap> bucket6 = MockBucket({{keyA, 2}});

    // Start time with no events.
    ASSERT_EQ(anomalyTracker.mSumOverPastBuckets.size(), 0u);
    EXPECT_EQ(anomalyTracker.mMostRecentBucketNum, -1LL);

    // Event from bucket #0 occurs.
    EXPECT_TRUE(detectAnomaliesPass(anomalyTracker, 0, bucket0, {}, {keyA, keyB, keyC}));
    detectAndDeclareAnomalies(anomalyTracker, 0, bucket0, eventTimestamp1);
    checkRefractoryTimes(anomalyTracker, eventTimestamp0, refractoryPeriodSec,
            {{keyA, -1}, {keyB, -1}, {keyC, -1}});

    // Adds past bucket #0
    anomalyTracker.addPastBucket(bucket0, 0);
    ASSERT_EQ(anomalyTracker.mSumOverPastBuckets.size(), 3u);
    EXPECT_EQ(anomalyTracker.getSumOverPastBuckets(keyA), 1LL);
    EXPECT_EQ(anomalyTracker.getSumOverPastBuckets(keyB), 2LL);
    EXPECT_EQ(anomalyTracker.getSumOverPastBuckets(keyC), 1LL);
    EXPECT_EQ(anomalyTracker.mMostRecentBucketNum, 0LL);

    // Event from bucket #1 occurs.
    EXPECT_TRUE(detectAnomaliesPass(anomalyTracker, 1, bucket1, {}, {keyA, keyB, keyC}));
    detectAndDeclareAnomalies(anomalyTracker, 1, bucket1, eventTimestamp1);
    checkRefractoryTimes(anomalyTracker, eventTimestamp1, refractoryPeriodSec,
            {{keyA, -1}, {keyB, -1}, {keyC, -1}});

    // Adds past bucket #0 again. The sum does not change.
    anomalyTracker.addPastBucket(bucket0, 0);
    ASSERT_EQ(anomalyTracker.mSumOverPastBuckets.size(), 3u);
    EXPECT_EQ(anomalyTracker.getSumOverPastBuckets(keyA), 1LL);
    EXPECT_EQ(anomalyTracker.getSumOverPastBuckets(keyB), 2LL);
    EXPECT_EQ(anomalyTracker.getSumOverPastBuckets(keyC), 1LL);
    EXPECT_EQ(anomalyTracker.mMostRecentBucketNum, 0LL);
    EXPECT_TRUE(detectAnomaliesPass(anomalyTracker, 1, bucket1, {}, {keyA, keyB, keyC}));
    detectAndDeclareAnomalies(anomalyTracker, 1, bucket1, eventTimestamp1 + 1);
    checkRefractoryTimes(anomalyTracker, eventTimestamp1, refractoryPeriodSec,
            {{keyA, -1}, {keyB, -1}, {keyC, -1}});

    // Adds past bucket #1.
    anomalyTracker.addPastBucket(bucket1, 1);
    EXPECT_EQ(anomalyTracker.mMostRecentBucketNum, 1L);
    ASSERT_EQ(anomalyTracker.mSumOverPastBuckets.size(), 3UL);
    EXPECT_EQ(anomalyTracker.getSumOverPastBuckets(keyA), 2LL);
    EXPECT_EQ(anomalyTracker.getSumOverPastBuckets(keyB), 2LL);
    EXPECT_EQ(anomalyTracker.getSumOverPastBuckets(keyC), 1LL);

    // Event from bucket #2 occurs. New anomaly on keyB.
    EXPECT_TRUE(detectAnomaliesPass(anomalyTracker, 2, bucket2, {keyB}, {keyA, keyC}));
    detectAndDeclareAnomalies(anomalyTracker, 2, bucket2, eventTimestamp2);
    checkRefractoryTimes(anomalyTracker, eventTimestamp2, refractoryPeriodSec,
            {{keyA, -1}, {keyB, eventTimestamp2}, {keyC, -1}});

    // Adds past bucket #1 again. Nothing changes.
    anomalyTracker.addPastBucket(bucket1, 1);
    EXPECT_EQ(anomalyTracker.mMostRecentBucketNum, 1L);
    ASSERT_EQ(anomalyTracker.mSumOverPastBuckets.size(), 3UL);
    EXPECT_EQ(anomalyTracker.getSumOverPastBuckets(keyA), 2LL);
    EXPECT_EQ(anomalyTracker.getSumOverPastBuckets(keyB), 2LL);
    EXPECT_EQ(anomalyTracker.getSumOverPastBuckets(keyC), 1LL);
    // Event from bucket #2 occurs (again).
    EXPECT_TRUE(detectAnomaliesPass(anomalyTracker, 2, bucket2, {keyB}, {keyA, keyC}));
    detectAndDeclareAnomalies(anomalyTracker, 2, bucket2, eventTimestamp2 + 1);
    checkRefractoryTimes(anomalyTracker, eventTimestamp2, refractoryPeriodSec,
            {{keyA, -1}, {keyB, eventTimestamp2}, {keyC, -1}});

    // Adds past bucket #2.
    anomalyTracker.addPastBucket(bucket2, 2);
    EXPECT_EQ(anomalyTracker.mMostRecentBucketNum, 2L);
    ASSERT_EQ(anomalyTracker.mSumOverPastBuckets.size(), 2UL);
    EXPECT_EQ(anomalyTracker.getSumOverPastBuckets(keyA), 1LL);
    EXPECT_EQ(anomalyTracker.getSumOverPastBuckets(keyB), 1LL);

    // Event from bucket #3 occurs. New anomaly on keyA.
    EXPECT_TRUE(detectAnomaliesPass(anomalyTracker, 3, bucket3, {keyA}, {keyB, keyC}));
    detectAndDeclareAnomalies(anomalyTracker, 3, bucket3, eventTimestamp3);
    checkRefractoryTimes(anomalyTracker, eventTimestamp3, refractoryPeriodSec,
            {{keyA, eventTimestamp3}, {keyB, eventTimestamp2}, {keyC, -1}});

    // Adds bucket #3.
    anomalyTracker.addPastBucket(bucket3, 3L);
    EXPECT_EQ(anomalyTracker.mMostRecentBucketNum, 3L);
    ASSERT_EQ(anomalyTracker.mSumOverPastBuckets.size(), 2UL);
    EXPECT_EQ(anomalyTracker.getSumOverPastBuckets(keyA), 2LL);
    EXPECT_EQ(anomalyTracker.getSumOverPastBuckets(keyB), 1LL);

    // Event from bucket #4 occurs. New anomaly on keyB.
    EXPECT_TRUE(detectAnomaliesPass(anomalyTracker, 4, bucket4, {keyB}, {keyA, keyC}));
    detectAndDeclareAnomalies(anomalyTracker, 4, bucket4, eventTimestamp4);
    checkRefractoryTimes(anomalyTracker, eventTimestamp4, refractoryPeriodSec,
            {{keyA, eventTimestamp3}, {keyB, eventTimestamp4}, {keyC, -1}});

    // Adds bucket #4.
    anomalyTracker.addPastBucket(bucket4, 4);
    EXPECT_EQ(anomalyTracker.mMostRecentBucketNum, 4L);
    ASSERT_EQ(anomalyTracker.mSumOverPastBuckets.size(), 2UL);
    EXPECT_EQ(anomalyTracker.getSumOverPastBuckets(keyA), 2LL);
    EXPECT_EQ(anomalyTracker.getSumOverPastBuckets(keyB), 5LL);

    // Event from bucket #5 occurs. New anomaly on keyA, which is still in refractory.
    EXPECT_TRUE(detectAnomaliesPass(anomalyTracker, 5, bucket5, {keyA, keyB}, {keyC}));
    detectAndDeclareAnomalies(anomalyTracker, 5, bucket5, eventTimestamp5);
    checkRefractoryTimes(anomalyTracker, eventTimestamp5, refractoryPeriodSec,
            {{keyA, eventTimestamp3}, {keyB, eventTimestamp4}, {keyC, -1}});

    // Adds bucket #5.
    anomalyTracker.addPastBucket(bucket5, 5);
    EXPECT_EQ(anomalyTracker.mMostRecentBucketNum, 5L);
    ASSERT_EQ(anomalyTracker.mSumOverPastBuckets.size(), 2UL);
    EXPECT_EQ(anomalyTracker.getSumOverPastBuckets(keyA), 2LL);
    EXPECT_EQ(anomalyTracker.getSumOverPastBuckets(keyB), 5LL);

    // Event from bucket #6 occurs. New anomaly on keyA, which is now out of refractory.
    EXPECT_TRUE(detectAnomaliesPass(anomalyTracker, 6, bucket6, {keyA, keyB}, {keyC}));
    detectAndDeclareAnomalies(anomalyTracker, 6, bucket6, eventTimestamp6);
    checkRefractoryTimes(anomalyTracker, eventTimestamp6, refractoryPeriodSec,
            {{keyA, eventTimestamp6}, {keyB, eventTimestamp4}, {keyC, -1}});
}

TEST(AnomalyTrackerTest, TestSparseBuckets) {
    const int64_t bucketSizeNs = 30 * NS_PER_SEC;
    const int32_t refractoryPeriodSec = 2 * bucketSizeNs / NS_PER_SEC;
    Alert alert;
    alert.set_num_buckets(3);
    alert.set_refractory_period_secs(refractoryPeriodSec);
    alert.set_trigger_if_sum_gt(2);

    AnomalyTracker anomalyTracker(alert, kConfigKey);
    MetricDimensionKey keyA = getMockMetricDimensionKey(1, "a");
    MetricDimensionKey keyB = getMockMetricDimensionKey(1, "b");
    MetricDimensionKey keyC = getMockMetricDimensionKey(1, "c");
    MetricDimensionKey keyD = getMockMetricDimensionKey(1, "d");
    MetricDimensionKey keyE = getMockMetricDimensionKey(1, "e");

    std::shared_ptr<DimToValMap> bucket9 = MockBucket({{keyA, 1}, {keyB, 2}, {keyC, 1}});
    std::shared_ptr<DimToValMap> bucket16 = MockBucket({{keyB, 4}});
    std::shared_ptr<DimToValMap> bucket18 = MockBucket({{keyB, 1}, {keyC, 1}});
    std::shared_ptr<DimToValMap> bucket20 = MockBucket({{keyB, 3}, {keyC, 1}});
    std::shared_ptr<DimToValMap> bucket25 = MockBucket({{keyD, 1}});
    std::shared_ptr<DimToValMap> bucket28 = MockBucket({{keyE, 2}});

    int64_t eventTimestamp1 = bucketSizeNs * 8 + 1;
    int64_t eventTimestamp2 = bucketSizeNs * 15 + 11;
    int64_t eventTimestamp3 = bucketSizeNs * 17 + 1;
    int64_t eventTimestamp4 = bucketSizeNs * 19 + 2;
    int64_t eventTimestamp5 = bucketSizeNs * 24 + 3;
    int64_t eventTimestamp6 = bucketSizeNs * 27 + 3;

    EXPECT_EQ(anomalyTracker.mMostRecentBucketNum, -1LL);
    ASSERT_EQ(anomalyTracker.mSumOverPastBuckets.size(), 0UL);
    EXPECT_TRUE(detectAnomaliesPass(anomalyTracker, 9, bucket9, {}, {keyA, keyB, keyC, keyD}));
    detectAndDeclareAnomalies(anomalyTracker, 9, bucket9, eventTimestamp1);
    checkRefractoryTimes(anomalyTracker, eventTimestamp1, refractoryPeriodSec,
            {{keyA, -1}, {keyB, -1}, {keyC, -1}, {keyD, -1}, {keyE, -1}});

    // Add past bucket #9
    anomalyTracker.addPastBucket(bucket9, 9);
    EXPECT_EQ(anomalyTracker.mMostRecentBucketNum, 9L);
    ASSERT_EQ(anomalyTracker.mSumOverPastBuckets.size(), 3UL);
    EXPECT_EQ(anomalyTracker.getSumOverPastBuckets(keyA), 1LL);
    EXPECT_EQ(anomalyTracker.getSumOverPastBuckets(keyB), 2LL);
    EXPECT_EQ(anomalyTracker.getSumOverPastBuckets(keyC), 1LL);
    EXPECT_TRUE(detectAnomaliesPass(anomalyTracker, 16, bucket16, {keyB}, {keyA, keyC, keyD}));
    ASSERT_EQ(anomalyTracker.mSumOverPastBuckets.size(), 0UL);
    EXPECT_EQ(anomalyTracker.mMostRecentBucketNum, 15L);
    detectAndDeclareAnomalies(anomalyTracker, 16, bucket16, eventTimestamp2);
    ASSERT_EQ(anomalyTracker.mSumOverPastBuckets.size(), 0UL);
    EXPECT_EQ(anomalyTracker.mMostRecentBucketNum, 15L);
    checkRefractoryTimes(anomalyTracker, eventTimestamp2, refractoryPeriodSec,
            {{keyA, -1}, {keyB, eventTimestamp2}, {keyC, -1}, {keyD, -1}, {keyE, -1}});

    // Add past bucket #16
    anomalyTracker.addPastBucket(bucket16, 16);
    EXPECT_EQ(anomalyTracker.mMostRecentBucketNum, 16L);
    ASSERT_EQ(anomalyTracker.mSumOverPastBuckets.size(), 1UL);
    EXPECT_EQ(anomalyTracker.getSumOverPastBuckets(keyB), 4LL);
    EXPECT_TRUE(detectAnomaliesPass(anomalyTracker, 18, bucket18, {keyB}, {keyA, keyC, keyD}));
    ASSERT_EQ(anomalyTracker.mSumOverPastBuckets.size(), 1UL);
    EXPECT_EQ(anomalyTracker.getSumOverPastBuckets(keyB), 4LL);
    // Within refractory period.
    detectAndDeclareAnomalies(anomalyTracker, 18, bucket18, eventTimestamp3);
    checkRefractoryTimes(anomalyTracker, eventTimestamp3, refractoryPeriodSec,
            {{keyA, -1}, {keyB, eventTimestamp2}, {keyC, -1}, {keyD, -1}, {keyE, -1}});
    ASSERT_EQ(anomalyTracker.mSumOverPastBuckets.size(), 1UL);
    EXPECT_EQ(anomalyTracker.getSumOverPastBuckets(keyB), 4LL);

    // Add past bucket #18
    anomalyTracker.addPastBucket(bucket18, 18);
    EXPECT_EQ(anomalyTracker.mMostRecentBucketNum, 18L);
    ASSERT_EQ(anomalyTracker.mSumOverPastBuckets.size(), 2UL);
    EXPECT_EQ(anomalyTracker.getSumOverPastBuckets(keyB), 1LL);
    EXPECT_EQ(anomalyTracker.getSumOverPastBuckets(keyC), 1LL);
    EXPECT_TRUE(detectAnomaliesPass(anomalyTracker, 20, bucket20, {keyB}, {keyA, keyC, keyD}));
    EXPECT_EQ(anomalyTracker.mMostRecentBucketNum, 19L);
    ASSERT_EQ(anomalyTracker.mSumOverPastBuckets.size(), 2UL);
    EXPECT_EQ(anomalyTracker.getSumOverPastBuckets(keyB), 1LL);
    EXPECT_EQ(anomalyTracker.getSumOverPastBuckets(keyC), 1LL);
    detectAndDeclareAnomalies(anomalyTracker, 20, bucket20, eventTimestamp4);
    checkRefractoryTimes(anomalyTracker, eventTimestamp4, refractoryPeriodSec,
            {{keyA, -1}, {keyB, eventTimestamp4}, {keyC, -1}, {keyD, -1}, {keyE, -1}});

    // Add bucket #18 again. Nothing changes.
    anomalyTracker.addPastBucket(bucket18, 18);
    EXPECT_EQ(anomalyTracker.mMostRecentBucketNum, 19L);
    ASSERT_EQ(anomalyTracker.mSumOverPastBuckets.size(), 2UL);
    EXPECT_EQ(anomalyTracker.getSumOverPastBuckets(keyB), 1LL);
    EXPECT_EQ(anomalyTracker.getSumOverPastBuckets(keyC), 1LL);
    EXPECT_TRUE(detectAnomaliesPass(anomalyTracker, 20, bucket20, {keyB}, {keyA, keyC, keyD}));
    ASSERT_EQ(anomalyTracker.mSumOverPastBuckets.size(), 2UL);
    EXPECT_EQ(anomalyTracker.getSumOverPastBuckets(keyB), 1LL);
    EXPECT_EQ(anomalyTracker.getSumOverPastBuckets(keyC), 1LL);
    detectAndDeclareAnomalies(anomalyTracker, 20, bucket20, eventTimestamp4 + 1);
    // Within refractory period.
    checkRefractoryTimes(anomalyTracker, eventTimestamp4 + 1, refractoryPeriodSec,
            {{keyA, -1}, {keyB, eventTimestamp4}, {keyC, -1}, {keyD, -1}, {keyE, -1}});

    // Add past bucket #20
    anomalyTracker.addPastBucket(bucket20, 20);
    EXPECT_EQ(anomalyTracker.mMostRecentBucketNum, 20L);
    ASSERT_EQ(anomalyTracker.mSumOverPastBuckets.size(), 2UL);
    EXPECT_EQ(anomalyTracker.getSumOverPastBuckets(keyB), 3LL);
    EXPECT_EQ(anomalyTracker.getSumOverPastBuckets(keyC), 1LL);
    EXPECT_TRUE(detectAnomaliesPass(anomalyTracker, 25, bucket25, {}, {keyA, keyB, keyC, keyD}));
    EXPECT_EQ(anomalyTracker.mMostRecentBucketNum, 24L);
    ASSERT_EQ(anomalyTracker.mSumOverPastBuckets.size(), 0UL);
    detectAndDeclareAnomalies(anomalyTracker, 25, bucket25, eventTimestamp5);
    checkRefractoryTimes(anomalyTracker, eventTimestamp5, refractoryPeriodSec,
            {{keyA, -1}, {keyB, eventTimestamp4}, {keyC, -1}, {keyD, -1}, {keyE, -1}});

    // Add past bucket #25
    anomalyTracker.addPastBucket(bucket25, 25);
    EXPECT_EQ(anomalyTracker.mMostRecentBucketNum, 25L);
    ASSERT_EQ(anomalyTracker.mSumOverPastBuckets.size(), 1UL);
    EXPECT_EQ(anomalyTracker.getSumOverPastBuckets(keyD), 1LL);
    EXPECT_TRUE(detectAnomaliesPass(anomalyTracker, 28, bucket28, {},
            {keyA, keyB, keyC, keyD, keyE}));
    EXPECT_EQ(anomalyTracker.mMostRecentBucketNum, 27L);
    ASSERT_EQ(anomalyTracker.mSumOverPastBuckets.size(), 0UL);
    detectAndDeclareAnomalies(anomalyTracker, 28, bucket28, eventTimestamp6);
    ASSERT_EQ(anomalyTracker.mSumOverPastBuckets.size(), 0UL);
    checkRefractoryTimes(anomalyTracker, eventTimestamp6, refractoryPeriodSec,
            {{keyA, -1}, {keyB, -1}, {keyC, -1}, {keyD, -1}, {keyE, -1}});

    // Updates current bucket #28.
    (*bucket28)[keyE] = 5;
    EXPECT_TRUE(detectAnomaliesPass(anomalyTracker, 28, bucket28, {keyE},
            {keyA, keyB, keyC, keyD}));
    EXPECT_EQ(anomalyTracker.mMostRecentBucketNum, 27L);
    ASSERT_EQ(anomalyTracker.mSumOverPastBuckets.size(), 0UL);
    detectAndDeclareAnomalies(anomalyTracker, 28, bucket28, eventTimestamp6 + 7);
    ASSERT_EQ(anomalyTracker.mSumOverPastBuckets.size(), 0UL);
    checkRefractoryTimes(anomalyTracker, eventTimestamp6, refractoryPeriodSec,
            {{keyA, -1}, {keyB, -1}, {keyC, -1}, {keyD, -1}, {keyE, eventTimestamp6 + 7}});
}

TEST(AnomalyTrackerTest, TestProbabilityOfInforming) {
    // Initiating StatsdStats at the start of this test, so it doesn't call rand() during the test
    StatsdStats::getInstance();
    srand(/*commonly used seed=*/0);
    const int64_t bucketSizeNs = 30 * NS_PER_SEC;
    const int32_t refractoryPeriodSec = bucketSizeNs / NS_PER_SEC;
    int broadcastSubRandId = 1, broadcastSubAlwaysId = 2, broadcastSubNeverId = 3;

    // Alert with probability of informing set to 0.5
    Alert alertRand = createAlert("alertRand", /*metric id=*/0, /*buckets=*/1, /*triggerSum=*/0);
    alertRand.set_refractory_period_secs(refractoryPeriodSec);
    alertRand.set_probability_of_informing(0.5);
    AnomalyTracker anomalyTrackerRand(alertRand, kConfigKey);

    Subscription subRand = createSubscription("subRand", /*rule_type=*/Subscription::ALERT,
                                              /*rule_id=*/alertRand.id());
    subRand.mutable_broadcast_subscriber_details()->set_subscriber_id(broadcastSubRandId);
    anomalyTrackerRand.addSubscription(subRand);

    // Alert with probability of informing set to 1.1 (always; set by default)
    Alert alertAlways =
            createAlert("alertAlways", /*metric id=*/0, /*buckets=*/1, /*triggerSum=*/0);
    alertAlways.set_refractory_period_secs(refractoryPeriodSec);
    AnomalyTracker anomalyTrackerAlways(alertAlways, kConfigKey);

    Subscription subAlways = createSubscription("subAlways", /*rule_type=*/Subscription::ALERT,
                                                /*rule_id=*/alertAlways.id());
    subAlways.mutable_broadcast_subscriber_details()->set_subscriber_id(broadcastSubAlwaysId);
    anomalyTrackerAlways.addSubscription(subAlways);

    // Alert with probability of informing set to -0.1 (never)
    Alert alertNever = createAlert("alertNever", /*metric id=*/0, /*buckets=*/1, /*triggerSum=*/0);
    alertNever.set_refractory_period_secs(refractoryPeriodSec);
    alertNever.set_probability_of_informing(-0.1);
    AnomalyTracker anomalyTrackerNever(alertNever, kConfigKey);

    Subscription subNever = createSubscription("subNever", /*rule_type=*/Subscription::ALERT,
                                               /*rule_id=*/alertNever.id());
    subNever.mutable_broadcast_subscriber_details()->set_subscriber_id(broadcastSubNeverId);
    anomalyTrackerNever.addSubscription(subNever);

    // Bucket value needs to be greater than 0 to detect and declare anomaly
    int bucketValue = 1;

    int alertRandCount = 0, alertAlwaysCount = 0;
    // The binder calls here will happen synchronously because they are in-process.
    shared_ptr<MockPendingIntentRef> randBroadcast =
            SharedRefBase::make<StrictMock<MockPendingIntentRef>>();
    EXPECT_CALL(*randBroadcast,
                sendSubscriberBroadcast(kConfigUid, kConfigId, subRand.id(), alertRand.id(), _, _))
            .Times(3)
            .WillRepeatedly([&alertRandCount] {
                alertRandCount++;
                return Status::ok();
            });

    shared_ptr<MockPendingIntentRef> alwaysBroadcast =
            SharedRefBase::make<StrictMock<MockPendingIntentRef>>();
    EXPECT_CALL(*alwaysBroadcast, sendSubscriberBroadcast(kConfigUid, kConfigId, subAlways.id(),
                                                          alertAlways.id(), _, _))
            .Times(10)
            .WillRepeatedly([&alertAlwaysCount] {
                alertAlwaysCount++;
                return Status::ok();
            });

    shared_ptr<MockPendingIntentRef> neverBroadcast =
            SharedRefBase::make<StrictMock<MockPendingIntentRef>>();
    EXPECT_CALL(*neverBroadcast, sendSubscriberBroadcast(kConfigUid, kConfigId, subNever.id(),
                                                         alertNever.id(), _, _))
            .Times(0);

    SubscriberReporter::getInstance().setBroadcastSubscriber(kConfigKey, broadcastSubRandId,
                                                             randBroadcast);
    SubscriberReporter::getInstance().setBroadcastSubscriber(kConfigKey, broadcastSubAlwaysId,
                                                             alwaysBroadcast);
    SubscriberReporter::getInstance().setBroadcastSubscriber(kConfigKey, broadcastSubNeverId,
                                                             neverBroadcast);

    // Trying to inform the subscription and start the refractory period countdown 10x.
    // Deterministic sequence for anomalyTrackerRand:
    // 0.96, 0.95, 0.95, 0.94, 0.43, 0.92, 0.92, 0.41, 0.39, 0.88
    for (size_t i = 0; i < 10; i++) {
        int64_t curEventTimestamp = bucketSizeNs * i;
        anomalyTrackerRand.detectAndDeclareAnomaly(curEventTimestamp, /*bucketNum=*/i,
                                                   /*metric_id=*/0, DEFAULT_METRIC_DIMENSION_KEY,
                                                   bucketValue);
        if (i <= 3) {
            EXPECT_EQ(alertRandCount, 0);
        } else if (i >= 4 && i <= 6) {
            EXPECT_EQ(alertRandCount, 1);
        } else if (i == 7) {
            EXPECT_EQ(alertRandCount, 2);
        } else {
            EXPECT_EQ(alertRandCount, 3);
        }
        anomalyTrackerAlways.detectAndDeclareAnomaly(curEventTimestamp, /*bucketNum=*/i,
                                                     /*metric_id=*/0, DEFAULT_METRIC_DIMENSION_KEY,
                                                     bucketValue);
        EXPECT_EQ(alertAlwaysCount, i + 1);
        anomalyTrackerNever.detectAndDeclareAnomaly(curEventTimestamp, /*bucketNum=*/i,
                                                    /*metric_id=*/0, DEFAULT_METRIC_DIMENSION_KEY,
                                                    bucketValue);
    }
    SubscriberReporter::getInstance().unsetBroadcastSubscriber(kConfigKey, broadcastSubRandId);
    SubscriberReporter::getInstance().unsetBroadcastSubscriber(kConfigKey, broadcastSubAlwaysId);
    SubscriberReporter::getInstance().unsetBroadcastSubscriber(kConfigKey, broadcastSubNeverId);
}

}  // namespace statsd
}  // namespace os
}  // namespace android
#else
GTEST_LOG_(INFO) << "This test does nothing.\n";
#endif
