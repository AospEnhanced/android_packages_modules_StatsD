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
#define STATSD_DEBUG false
#include "Log.h"
#include "OringDurationTracker.h"
#include "guardrail/StatsdStats.h"

namespace android {
namespace os {
namespace statsd {

using std::pair;

OringDurationTracker::OringDurationTracker(const ConfigKey& key, const int64_t id,
                                           const MetricDimensionKey& eventKey,
                                           const sp<ConditionWizard>& wizard, int conditionIndex,
                                           bool nesting, int64_t currentBucketStartNs,
                                           int64_t currentBucketNum, int64_t startTimeNs,
                                           int64_t bucketSizeNs, bool conditionSliced,
                                           bool fullLink,
                                           const vector<sp<AnomalyTracker>>& anomalyTrackers)
    : DurationTracker(key, id, eventKey, wizard, conditionIndex, nesting, currentBucketStartNs,
                      currentBucketNum, startTimeNs, bucketSizeNs, conditionSliced, fullLink,
                      anomalyTrackers),
      mStarted(),
      mPaused() {
    mLastStartTime = 0;
}

bool OringDurationTracker::hitGuardRail(const HashableDimensionKey& newKey,
                                        size_t dimensionHardLimit) const {
    // ===========GuardRail==============
    // 1. Report the tuple count if the tuple count > soft limit
    if (mConditionKeyMap.find(newKey) != mConditionKeyMap.end()) {
        return false;
    }
    if (mConditionKeyMap.size() >= StatsdStats::kDimensionKeySizeSoftLimit) {
        size_t newTupleCount = mConditionKeyMap.size() + 1;
        StatsdStats::getInstance().noteMetricDimensionSize(mConfigKey, mTrackerId, newTupleCount);
        // 2. Don't add more tuples, we are above the allowed threshold. Drop the data.
        if (newTupleCount > dimensionHardLimit) {
            if (!mHasHitGuardrail) {
                ALOGE("OringDurTracker %lld dropping data for dimension key %s",
                      (long long)mTrackerId, newKey.toString().c_str());
                mHasHitGuardrail = true;
            }
            StatsdStats::getInstance().noteHardDimensionLimitReached(mTrackerId);
            return true;
        }
    }
    return false;
}

void OringDurationTracker::noteStart(const HashableDimensionKey& key, bool condition,
                                     const int64_t eventTime, const ConditionKey& conditionKey,
                                     size_t dimensionHardLimit) {
    if (hitGuardRail(key, dimensionHardLimit)) {
        return;
    }
    if (condition) {
        if (mStarted.size() == 0) {
            mLastStartTime = eventTime;
            VLOG("record first start....");
            startAnomalyAlarm(eventTime);
        }
        mStarted[key]++;
    } else {
        mPaused[key]++;
    }

    if (mConditionSliced && mConditionKeyMap.find(key) == mConditionKeyMap.end()) {
        mConditionKeyMap[key] = conditionKey;
    }
    VLOG("Oring: %s start, condition %d", key.toString().c_str(), condition);
}

void OringDurationTracker::noteStop(const HashableDimensionKey& key, const int64_t timestamp,
                                    const bool stopAll) {
    VLOG("Oring: %s stop", key.toString().c_str());
    auto it = mStarted.find(key);
    if (it != mStarted.end()) {
        (it->second)--;
        if (stopAll || !mNested || it->second <= 0) {
            mStarted.erase(it);
            mConditionKeyMap.erase(key);
        }
        if (mStarted.empty()) {
            mStateKeyDurationMap[mEventKey.getStateValuesKey()].mDuration +=
                    (timestamp - mLastStartTime);
            detectAndDeclareAnomaly(
                    timestamp, mCurrentBucketNum,
                    getCurrentStateKeyDuration() + getCurrentStateKeyFullBucketDuration());
            VLOG("record duration %lld, total duration %lld for state key %s",
                 (long long)timestamp - mLastStartTime, (long long)getCurrentStateKeyDuration(),
                 mEventKey.getStateValuesKey().toString().c_str());
        }
    }

    auto pausedIt = mPaused.find(key);
    if (pausedIt != mPaused.end()) {
        (pausedIt->second)--;
        if (stopAll || !mNested || pausedIt->second <= 0) {
            mPaused.erase(pausedIt);
            mConditionKeyMap.erase(key);
        }
    }
    if (mStarted.empty()) {
        stopAnomalyAlarm(timestamp);
    }
}

void OringDurationTracker::noteStopAll(const int64_t timestamp) {
    if (!mStarted.empty()) {
        mStateKeyDurationMap[mEventKey.getStateValuesKey()].mDuration +=
                (timestamp - mLastStartTime);
        VLOG("Oring Stop all: record duration %lld, total duration %lld for state key %s",
             (long long)timestamp - mLastStartTime, (long long)getCurrentStateKeyDuration(),
             mEventKey.getStateValuesKey().toString().c_str());
        detectAndDeclareAnomaly(
                timestamp, mCurrentBucketNum,
                getCurrentStateKeyDuration() + getCurrentStateKeyFullBucketDuration());
    }

    stopAnomalyAlarm(timestamp);
    mStarted.clear();
    mPaused.clear();
    mConditionKeyMap.clear();
}

bool OringDurationTracker::flushCurrentBucket(
        const int64_t eventTimeNs, const optional<UploadThreshold>& uploadThreshold,
        const int64_t globalConditionTrueNs,
        std::unordered_map<MetricDimensionKey, std::vector<DurationBucket>>* output) {
    VLOG("OringDurationTracker Flushing.............");

    // Note that we have to mimic the bucket time changes we do in the
    // MetricProducer#notifyAppUpgrade.

    int numBucketsForward = 0;
    int64_t fullBucketEnd = getCurrentBucketEndTimeNs();
    int64_t currentBucketEndTimeNs;

    bool isFullBucket = eventTimeNs >= fullBucketEnd;
    if (isFullBucket) {
        numBucketsForward = 1 + (eventTimeNs - fullBucketEnd) / mBucketSizeNs;
        currentBucketEndTimeNs = fullBucketEnd;
    } else {
        // This must be a partial bucket.
        currentBucketEndTimeNs = eventTimeNs;
    }

    // Process the current bucket.
    if (mStarted.size() > 0) {
        // Calculate the duration for the current state key.
        mStateKeyDurationMap[mEventKey.getStateValuesKey()].mDuration +=
                (currentBucketEndTimeNs - mLastStartTime);
    }
    // Store DurationBucket info for each whatKey, stateKey pair.
    // Note: The whatKey stored in mEventKey is constant for each DurationTracker, while the
    // stateKey stored in mEventKey is only the current stateKey. mStateKeyDurationMap is used to
    // store durations for each stateKey, so we need to flush the bucket by creating a
    // DurationBucket for each stateKey.
    for (auto& durationIt : mStateKeyDurationMap) {
        durationIt.second.mDurationFullBucket += durationIt.second.mDuration;
        if (durationPassesThreshold(uploadThreshold, durationIt.second.mDuration)) {
            DurationBucket current_info;
            current_info.mBucketStartNs = mCurrentBucketStartTimeNs;
            current_info.mBucketEndNs = currentBucketEndTimeNs;
            current_info.mDuration = durationIt.second.mDuration;
            current_info.mConditionTrueNs = globalConditionTrueNs;
            (*output)[MetricDimensionKey(mEventKey.getDimensionKeyInWhat(), durationIt.first)]
                    .push_back(current_info);
            VLOG("  duration: %lld", (long long)current_info.mDuration);
        } else {
            VLOG("  duration: %lld does not pass set threshold",
                 (long long)durationIt.second.mDuration);
        }

        if (isFullBucket) {
            // End of full bucket, can send to anomaly tracker now.
            addPastBucketToAnomalyTrackers(
                    MetricDimensionKey(mEventKey.getDimensionKeyInWhat(), durationIt.first),
                    getCurrentStateKeyFullBucketDuration(), mCurrentBucketNum);
        }
        durationIt.second.mDuration = 0;
    }
    // Full bucket is only needed when we have anomaly trackers.
    if (isFullBucket || mAnomalyTrackers.empty()) {
        mStateKeyDurationMap.clear();
    }

    if (mStarted.size() > 0) {
        for (int i = 1; i < numBucketsForward; i++) {
            DurationBucket info;
            info.mBucketStartNs = fullBucketEnd + mBucketSizeNs * (i - 1);
            info.mBucketEndNs = info.mBucketStartNs + mBucketSizeNs;
            info.mDuration = mBucketSizeNs;
            // Full duration buckets are attributed to the current stateKey.
            (*output)[mEventKey].push_back(info);
            // Safe to send these buckets to anomaly tracker since they must be full buckets.
            // If it's a partial bucket, numBucketsForward would be 0.
            addPastBucketToAnomalyTrackers(mEventKey, info.mDuration, mCurrentBucketNum + i);
            VLOG("  add filling bucket with duration %lld", (long long)info.mDuration);
        }
    } else {
        if (numBucketsForward >= 2) {
            addPastBucketToAnomalyTrackers(mEventKey, 0, mCurrentBucketNum + numBucketsForward - 1);
        }
    }

    if (numBucketsForward > 0) {
        mCurrentBucketStartTimeNs = fullBucketEnd + (numBucketsForward - 1) * mBucketSizeNs;
        mCurrentBucketNum += numBucketsForward;
    } else {  // We must be forming a partial bucket.
        mCurrentBucketStartTimeNs = eventTimeNs;
    }
    mLastStartTime = mCurrentBucketStartTimeNs;
    // Reset mHasHitGuardrail boolean since bucket was reset
    mHasHitGuardrail = false;

    // If all stopped, then tell owner it's safe to remove this tracker on a full bucket.
    // On a partial bucket, only clear if no anomaly trackers, as full bucket duration is used
    // for anomaly detection.
    // Note: Anomaly trackers can be added on config updates, in which case mAnomalyTrackers > 0 and
    // the full bucket duration could be used, but this is very rare so it is okay to clear.
    return mStarted.empty() && mPaused.empty() && (isFullBucket || mAnomalyTrackers.size() == 0);
}

bool OringDurationTracker::flushIfNeeded(
        int64_t eventTimeNs, const optional<UploadThreshold>& uploadThreshold,
        unordered_map<MetricDimensionKey, vector<DurationBucket>>* output) {
    if (eventTimeNs < getCurrentBucketEndTimeNs()) {
        return false;
    }
    return flushCurrentBucket(eventTimeNs, uploadThreshold, /*globalConditionTrueNs=*/0, output);
}

void OringDurationTracker::onSlicedConditionMayChange(const int64_t timestamp) {
    vector<pair<HashableDimensionKey, int>> startedToPaused;
    vector<pair<HashableDimensionKey, int>> pausedToStarted;
    if (!mStarted.empty()) {
        for (auto it = mStarted.begin(); it != mStarted.end();) {
            const auto& key = it->first;
            const auto& condIt = mConditionKeyMap.find(key);
            if (condIt == mConditionKeyMap.end()) {
                VLOG("Key %s dont have condition key", key.toString().c_str());
                ++it;
                continue;
            }
            ConditionState conditionState =
                mWizard->query(mConditionTrackerIndex, condIt->second,
                               !mHasLinksToAllConditionDimensionsInTracker);
            if (conditionState != ConditionState::kTrue) {
                startedToPaused.push_back(*it);
                it = mStarted.erase(it);
                VLOG("Key %s started -> paused", key.toString().c_str());
            } else {
                ++it;
            }
        }

        if (mStarted.empty()) {
            mStateKeyDurationMap[mEventKey.getStateValuesKey()].mDuration +=
                    (timestamp - mLastStartTime);
            VLOG("record duration %lld, total duration %lld for state key %s",
                 (long long)(timestamp - mLastStartTime), (long long)getCurrentStateKeyDuration(),
                 mEventKey.getStateValuesKey().toString().c_str());
            detectAndDeclareAnomaly(
                    timestamp, mCurrentBucketNum,
                    getCurrentStateKeyDuration() + getCurrentStateKeyFullBucketDuration());
        }
    }

    if (!mPaused.empty()) {
        for (auto it = mPaused.begin(); it != mPaused.end();) {
            const auto& key = it->first;
            if (mConditionKeyMap.find(key) == mConditionKeyMap.end()) {
                VLOG("Key %s dont have condition key", key.toString().c_str());
                ++it;
                continue;
            }
            ConditionState conditionState =
                mWizard->query(mConditionTrackerIndex, mConditionKeyMap[key],
                               !mHasLinksToAllConditionDimensionsInTracker);
            if (conditionState == ConditionState::kTrue) {
                pausedToStarted.push_back(*it);
                it = mPaused.erase(it);
                VLOG("Key %s paused -> started", key.toString().c_str());
            } else {
                ++it;
            }
        }

        if (mStarted.empty() && pausedToStarted.size() > 0) {
            mLastStartTime = timestamp;
        }
    }

    if (mStarted.empty() && !pausedToStarted.empty()) {
        startAnomalyAlarm(timestamp);
    }
    mStarted.insert(pausedToStarted.begin(), pausedToStarted.end());
    mPaused.insert(startedToPaused.begin(), startedToPaused.end());

    if (mStarted.empty()) {
        stopAnomalyAlarm(timestamp);
    }
}

void OringDurationTracker::onConditionChanged(bool condition, const int64_t timestamp) {
    if (condition) {
        if (!mPaused.empty()) {
            VLOG("Condition true, all started");
            if (mStarted.empty()) {
                mLastStartTime = timestamp;
            }
            if (mStarted.empty() && !mPaused.empty()) {
                startAnomalyAlarm(timestamp);
            }
            mStarted.insert(mPaused.begin(), mPaused.end());
            mPaused.clear();
        }
    } else {
        if (!mStarted.empty()) {
            VLOG("Condition false, all paused");
            mStateKeyDurationMap[mEventKey.getStateValuesKey()].mDuration +=
                    (timestamp - mLastStartTime);
            mPaused.insert(mStarted.begin(), mStarted.end());
            mStarted.clear();
            detectAndDeclareAnomaly(
                    timestamp, mCurrentBucketNum,
                    getCurrentStateKeyDuration() + getCurrentStateKeyFullBucketDuration());
        }
    }
    if (mStarted.empty()) {
        stopAnomalyAlarm(timestamp);
    }
}

void OringDurationTracker::onStateChanged(const int64_t timestamp, const int32_t atomId,
                                          const FieldValue& newState) {
    // Nothing needs to be done on a state change if we have not seen a start
    // event, the metric is currently not active, or condition is false.
    // For these cases, no keys are being tracked in mStarted, so update
    // the current state key and return.
    if (mStarted.empty()) {
        updateCurrentStateKey(atomId, newState);
        return;
    }
    // Add the current duration length to the previous state key and then update
    // the last start time and current state key.
    mStateKeyDurationMap[mEventKey.getStateValuesKey()].mDuration += (timestamp - mLastStartTime);
    mLastStartTime = timestamp;
    updateCurrentStateKey(atomId, newState);
}

bool OringDurationTracker::hasAccumulatedDuration() const {
    return !mStarted.empty() || !mPaused.empty() || !mStateKeyDurationMap.empty();
}

bool OringDurationTracker::hasStartedDuration() const {
    return !mStarted.empty();
}

int64_t OringDurationTracker::predictAnomalyTimestampNs(const AnomalyTracker& anomalyTracker,
                                                        const int64_t eventTimestampNs) const {
    // The anomaly threshold.
    const int64_t thresholdNs = anomalyTracker.getAnomalyThreshold();

    // The timestamp of the current bucket end.
    const int64_t currentBucketEndNs = getCurrentBucketEndTimeNs();

    // The past duration ns for the current bucket of the current stateKey.
    int64_t currentStateBucketPastNs =
            getCurrentStateKeyDuration() + getCurrentStateKeyFullBucketDuration();

    // As we move into the future, old buckets get overwritten (so their old data is erased).
    // Sum of past durations. Will change as we overwrite old buckets.
    int64_t pastNs = currentStateBucketPastNs + anomalyTracker.getSumOverPastBuckets(mEventKey);

    // The refractory period end timestamp for dimension mEventKey.
    const int64_t refractoryPeriodEndNs =
            anomalyTracker.getRefractoryPeriodEndsSec(mEventKey) * NS_PER_SEC;

    // The anomaly should happen when accumulated wakelock duration is above the threshold and
    // not within the refractory period.
    int64_t anomalyTimestampNs =
        std::max(eventTimestampNs + thresholdNs - pastNs, refractoryPeriodEndNs);
    // If the predicted the anomaly timestamp is within the current bucket, return it directly.
    if (anomalyTimestampNs <= currentBucketEndNs) {
        return std::max(eventTimestampNs, anomalyTimestampNs);
    }

    // Remove the old bucket.
    if (anomalyTracker.getNumOfPastBuckets() > 0) {
        pastNs -= anomalyTracker.getPastBucketValue(
                            mEventKey,
                            mCurrentBucketNum - anomalyTracker.getNumOfPastBuckets());
        // Add the remaining of the current bucket to the accumulated wakelock duration.
        pastNs += (currentBucketEndNs - eventTimestampNs);
    } else {
        // The anomaly depends on only one bucket.
        pastNs = 0;
    }

    // The anomaly will not happen in the current bucket. We need to iterate over the future buckets
    // to predict the accumulated wakelock duration and determine the anomaly timestamp accordingly.
    for (int futureBucketIdx = 1; futureBucketIdx <= anomalyTracker.getNumOfPastBuckets() + 1;
            futureBucketIdx++) {
        // The alarm candidate timestamp should meet two requirements:
        // 1. the accumulated wakelock duration is above the threshold.
        // 2. it is not within the refractory period.
        // 3. the alarm timestamp falls in this bucket. Otherwise we need to flush the past buckets,
        //    find the new alarm candidate timestamp and check these requirements again.
        const int64_t bucketEndNs = currentBucketEndNs + futureBucketIdx * mBucketSizeNs;
        int64_t anomalyTimestampNs =
            std::max(bucketEndNs - mBucketSizeNs + thresholdNs - pastNs, refractoryPeriodEndNs);
        if (anomalyTimestampNs <= bucketEndNs) {
            return anomalyTimestampNs;
        }
        if (anomalyTracker.getNumOfPastBuckets() <= 0) {
            continue;
        }

        // No valid alarm timestamp is found in this bucket. The clock moves to the end of the
        // bucket. Update the pastNs.
        pastNs += mBucketSizeNs;
        // 1. If the oldest past bucket is still in the past bucket window, we could fetch the past
        // bucket and erase it from pastNs.
        // 2. If the oldest past bucket is the current bucket, we should compute the
        //   wakelock duration in the current bucket and erase it from pastNs.
        // 3. Otherwise all othe past buckets are ancient.
        if (futureBucketIdx < anomalyTracker.getNumOfPastBuckets()) {
            pastNs -= anomalyTracker.getPastBucketValue(
                    mEventKey,
                    mCurrentBucketNum - anomalyTracker.getNumOfPastBuckets() + futureBucketIdx);
        } else if (futureBucketIdx == anomalyTracker.getNumOfPastBuckets()) {
            pastNs -= (currentStateBucketPastNs + (currentBucketEndNs - eventTimestampNs));
        }
    }

    return std::max(eventTimestampNs + thresholdNs, refractoryPeriodEndNs);
}

void OringDurationTracker::dumpStates(int out, bool verbose) const {
    dprintf(out, "\t\t started count %lu\n", (unsigned long)mStarted.size());
    dprintf(out, "\t\t paused count %lu\n", (unsigned long)mPaused.size());
    dprintf(out, "\t\t current duration %lld\n", (long long)getCurrentStateKeyDuration());
}

int64_t OringDurationTracker::getCurrentStateKeyDuration() const {
    auto it = mStateKeyDurationMap.find(mEventKey.getStateValuesKey());
    if (it == mStateKeyDurationMap.end()) {
        return 0;
    } else {
        return it->second.mDuration;
    }
}

int64_t OringDurationTracker::getCurrentStateKeyFullBucketDuration() const {
    auto it = mStateKeyDurationMap.find(mEventKey.getStateValuesKey());
    if (it == mStateKeyDurationMap.end()) {
        return 0;
    } else {
        return it->second.mDurationFullBucket;
    }
}

void OringDurationTracker::updateCurrentStateKey(const int32_t atomId, const FieldValue& newState) {
    HashableDimensionKey* stateValuesKey = mEventKey.getMutableStateValuesKey();
    for (size_t i = 0; i < stateValuesKey->getValues().size(); i++) {
        if (stateValuesKey->getValues()[i].mField.getTag() == atomId) {
            stateValuesKey->mutableValue(i)->mValue = newState.mValue;
        }
    }
}

}  // namespace statsd
}  // namespace os
}  // namespace android
