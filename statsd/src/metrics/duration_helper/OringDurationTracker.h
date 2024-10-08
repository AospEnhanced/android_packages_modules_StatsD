/*
 * Copyright (C) 2017 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef ORING_DURATION_TRACKER_H
#define ORING_DURATION_TRACKER_H

#include "DurationTracker.h"

namespace android {
namespace os {
namespace statsd {

// Tracks the "Or'd" duration -- if 2 durations are overlapping, they won't be double counted.
class OringDurationTracker : public DurationTracker {
public:
    OringDurationTracker(const ConfigKey& key, const int64_t id, const MetricDimensionKey& eventKey,
                         const sp<ConditionWizard>& wizard, int conditionIndex, bool nesting,
                         int64_t currentBucketStartNs, int64_t currentBucketNum,
                         int64_t startTimeNs, int64_t bucketSizeNs, bool conditionSliced,
                         bool fullLink, const std::vector<sp<AnomalyTracker>>& anomalyTrackers);

    OringDurationTracker(const OringDurationTracker& tracker) = default;

    void noteStart(const HashableDimensionKey& key, bool condition, int64_t eventTime,
                   const ConditionKey& conditionKey, size_t dimensionHardLimit) override;
    void noteStop(const HashableDimensionKey& key, int64_t eventTime, const bool stopAll) override;
    void noteStopAll(const int64_t eventTime) override;

    void onSlicedConditionMayChange(const int64_t timestamp) override;
    void onConditionChanged(bool condition, int64_t timestamp) override;

    void onStateChanged(const int64_t timestamp, const int32_t atomId,
                        const FieldValue& newState) override;

    bool flushCurrentBucket(
            int64_t eventTimeNs, const optional<UploadThreshold>& uploadThreshold,
            const int64_t globalConditionTrueNs,
            std::unordered_map<MetricDimensionKey, std::vector<DurationBucket>>* output) override;
    bool flushIfNeeded(
            int64_t timestampNs, const optional<UploadThreshold>& uploadThreshold,
            std::unordered_map<MetricDimensionKey, std::vector<DurationBucket>>* output) override;

    int64_t predictAnomalyTimestampNs(const AnomalyTracker& anomalyTracker,
                                      const int64_t currentTimestamp) const override;
    void dumpStates(int out, bool verbose) const override;

    int64_t getCurrentStateKeyDuration() const override;

    int64_t getCurrentStateKeyFullBucketDuration() const override;

    void updateCurrentStateKey(int32_t atomId, const FieldValue& newState);

    bool hasAccumulatedDuration() const override;

protected:
    // Returns true if at least one of the mInfos is started.
    bool hasStartedDuration() const override;

private:
    // We don't need to keep track of individual durations. The information that's needed is:
    // 1) which keys are started. We record the first start time.
    // 2) which keys are paused (started but condition was false)
    // 3) whenever a key stops, we remove it from the started set. And if the set becomes empty,
    //    it means everything has stopped, we then record the end time.
    std::unordered_map<HashableDimensionKey, int> mStarted;
    std::unordered_map<HashableDimensionKey, int> mPaused;
    int64_t mLastStartTime;
    std::unordered_map<HashableDimensionKey, ConditionKey> mConditionKeyMap;

    // return true if we should not allow newKey to be tracked because we are above the threshold
    bool hitGuardRail(const HashableDimensionKey& newKey, size_t dimensionHardLimit) const;

    FRIEND_TEST(OringDurationTrackerTest, TestDurationOverlap);
    FRIEND_TEST(OringDurationTrackerTest, TestCrossBucketBoundary);
    FRIEND_TEST(OringDurationTrackerTest, TestDurationConditionChange);
    FRIEND_TEST(OringDurationTrackerTest, TestPredictAnomalyTimestamp);
    FRIEND_TEST(OringDurationTrackerTest, TestAnomalyDetectionExpiredAlarm);
    FRIEND_TEST(OringDurationTrackerTest, TestAnomalyDetectionFiredAlarm);
    FRIEND_TEST(OringDurationTrackerTest, TestUploadThreshold);
    FRIEND_TEST(OringDurationTrackerTest, TestClearStateKeyMapWhenBucketFull);
    FRIEND_TEST(OringDurationTrackerTest, TestClearStateKeyMapWhenNoTrackers);
};

}  // namespace statsd
}  // namespace os
}  // namespace android

#endif  // ORING_DURATION_TRACKER_H
