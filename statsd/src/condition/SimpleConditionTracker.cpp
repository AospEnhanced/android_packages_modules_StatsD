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

#define STATSD_DEBUG false  // STOPSHIP if true
#include "Log.h"

#include "SimpleConditionTracker.h"
#include "guardrail/StatsdStats.h"

namespace android {
namespace os {
namespace statsd {

using std::unordered_map;

SimpleConditionTracker::SimpleConditionTracker(
        const ConfigKey& key, const int64_t id, const uint64_t protoHash, const int index,
        const SimplePredicate& simplePredicate,
        const unordered_map<int64_t, int>& atomMatchingTrackerMap)
    : ConditionTracker(id, index, protoHash),
      mConfigKey(key),
      mContainANYPositionInInternalDimensions(false) {
    VLOG("creating SimpleConditionTracker %lld", (long long)mConditionId);
    mCountNesting = simplePredicate.count_nesting();

    setMatcherIndices(simplePredicate, atomMatchingTrackerMap);

    if (simplePredicate.has_dimensions()) {
        translateFieldMatcher(simplePredicate.dimensions(), &mOutputDimensions);
        if (mOutputDimensions.size() > 0) {
            mSliced = true;
        }
        mContainANYPositionInInternalDimensions = HasPositionANY(simplePredicate.dimensions());
    }
    // If an initial value isn't specified, default to false if sliced and unknown if not sliced.
    mInitialValue = simplePredicate.has_initial_value()
                            ? convertInitialValue(simplePredicate.initial_value())
                            : mSliced ? ConditionState::kFalse : ConditionState::kUnknown;
    mInitialized = true;
}

SimpleConditionTracker::~SimpleConditionTracker() {
    VLOG("~SimpleConditionTracker()");
}

optional<InvalidConfigReason> SimpleConditionTracker::init(
        const vector<Predicate>& allConditionConfig,
        const vector<sp<ConditionTracker>>& allConditionTrackers,
        const unordered_map<int64_t, int>& conditionIdIndexMap, vector<uint8_t>& stack,
        vector<ConditionState>& conditionCache) {
    // SimpleConditionTracker does not have dependency on other conditions, thus we just return
    // if the initialization was successful.
    ConditionKey conditionKey;
    if (mSliced) {
        conditionKey[mConditionId] = DEFAULT_DIMENSION_KEY;
    }
    isConditionMet(conditionKey, allConditionTrackers, mSliced, conditionCache);
    if (!mInitialized) {
        return createInvalidConfigReasonWithPredicate(
                INVALID_CONFIG_REASON_CONDITION_TRACKER_NOT_INITIALIZED, mConditionId);
    }
    return nullopt;
}

optional<InvalidConfigReason> SimpleConditionTracker::onConfigUpdated(
        const vector<Predicate>& allConditionProtos, const int index,
        const vector<sp<ConditionTracker>>& allConditionTrackers,
        const unordered_map<int64_t, int>& atomMatchingTrackerMap,
        const unordered_map<int64_t, int>& conditionTrackerMap) {
    ConditionTracker::onConfigUpdated(allConditionProtos, index, allConditionTrackers,
                                      atomMatchingTrackerMap, conditionTrackerMap);
    setMatcherIndices(allConditionProtos[index].simple_predicate(), atomMatchingTrackerMap);
    return nullopt;
}

void SimpleConditionTracker::setMatcherIndices(
        const SimplePredicate& simplePredicate,
        const unordered_map<int64_t, int>& atomMatchingTrackerMap) {
    mTrackerIndex.clear();
    if (simplePredicate.has_start()) {
        auto pair = atomMatchingTrackerMap.find(simplePredicate.start());
        if (pair == atomMatchingTrackerMap.end()) {
            ALOGW("Start matcher %lld not found in the config", (long long)simplePredicate.start());
            return;
        }
        mStartLogMatcherIndex = pair->second;
        mTrackerIndex.insert(mStartLogMatcherIndex);
    } else {
        mStartLogMatcherIndex = -1;
    }

    if (simplePredicate.has_stop()) {
        auto pair = atomMatchingTrackerMap.find(simplePredicate.stop());
        if (pair == atomMatchingTrackerMap.end()) {
            ALOGW("Stop matcher %lld not found in the config", (long long)simplePredicate.stop());
            return;
        }
        mStopLogMatcherIndex = pair->second;
        mTrackerIndex.insert(mStopLogMatcherIndex);
    } else {
        mStopLogMatcherIndex = -1;
    }

    if (simplePredicate.has_stop_all()) {
        auto pair = atomMatchingTrackerMap.find(simplePredicate.stop_all());
        if (pair == atomMatchingTrackerMap.end()) {
            ALOGW("Stop all matcher %lld found in the config",
                  (long long)simplePredicate.stop_all());
            return;
        }
        mStopAllLogMatcherIndex = pair->second;
        mTrackerIndex.insert(mStopAllLogMatcherIndex);
    } else {
        mStopAllLogMatcherIndex = -1;
    }
}

void SimpleConditionTracker::dumpState() {
    VLOG("%lld DUMP:", (long long)mConditionId);
    for (const auto& pair : mSlicedConditionState) {
        VLOG("\t%s : %d", pair.first.toString().c_str(), pair.second);
    }

    VLOG("Changed to true keys: \n");
    for (const auto& key : mLastChangedToTrueDimensions) {
        VLOG("%s", key.toString().c_str());
    }
    VLOG("Changed to false keys: \n");
    for (const auto& key : mLastChangedToFalseDimensions) {
        VLOG("%s", key.toString().c_str());
    }
}

void SimpleConditionTracker::handleStopAll(std::vector<ConditionState>& conditionCache,
                                           std::vector<uint8_t>& conditionChangedCache) {
    // Unless the default condition is false, and there was nothing started, otherwise we have
    // triggered a condition change.
    conditionChangedCache[mIndex] =
            (mInitialValue == ConditionState::kFalse && mSlicedConditionState.empty()) ? false
                                                                                           : true;

    for (const auto& cond : mSlicedConditionState) {
        if (cond.second > 0) {
            mLastChangedToFalseDimensions.insert(cond.first);
        }
    }

    // After StopAll, we know everything has stopped. From now on, default condition is false.
    mInitialValue = ConditionState::kFalse;
    mSlicedConditionState.clear();
    conditionCache[mIndex] = ConditionState::kFalse;
}

bool SimpleConditionTracker::hitGuardRail(const HashableDimensionKey& newKey) const {
    if (!mSliced || mSlicedConditionState.find(newKey) != mSlicedConditionState.end()) {
        // if the condition is not sliced or the key is not new, we are good!
        return false;
    }
    // 1. Report the tuple count if the tuple count > soft limit
    if (mSlicedConditionState.size() >= StatsdStats::kDimensionKeySizeSoftLimit) {
        size_t newTupleCount = mSlicedConditionState.size() + 1;
        StatsdStats::getInstance().noteConditionDimensionSize(mConfigKey, mConditionId, newTupleCount);
        // 2. Don't add more tuples, we are above the allowed threshold. Drop the data.
        if (newTupleCount > StatsdStats::kDimensionKeySizeHardLimit) {
            ALOGE("Predicate %lld dropping data for dimension key %s",
                (long long)mConditionId, newKey.toString().c_str());
            return true;
        }
    }
    return false;
}

void SimpleConditionTracker::handleConditionEvent(const HashableDimensionKey& outputKey,
                                                  bool matchStart, ConditionState* conditionCache,
                                                  bool* conditionChangedCache) {
    bool changed = false;
    auto outputIt = mSlicedConditionState.find(outputKey);
    ConditionState newCondition;
    if (hitGuardRail(outputKey)) {
        (*conditionChangedCache) = false;
        // Tells the caller it's evaluated.
        (*conditionCache) = ConditionState::kUnknown;
        return;
    }
    if (outputIt == mSlicedConditionState.end()) {
        // We get a new output key.
        newCondition = matchStart ? ConditionState::kTrue : ConditionState::kFalse;
        if (matchStart && mInitialValue != ConditionState::kTrue) {
            mSlicedConditionState[outputKey] = 1;
            changed = true;
            mLastChangedToTrueDimensions.insert(outputKey);
        } else if (mInitialValue != ConditionState::kFalse) {
            // it's a stop and we don't have history about it.
            // If the default condition is not false, it means this stop is valuable to us.
            mSlicedConditionState[outputKey] = 0;
            mLastChangedToFalseDimensions.insert(outputKey);
            changed = true;
        }
    } else {
        // we have history about this output key.
        auto& startedCount = outputIt->second;
        // assign the old value first.
        newCondition = startedCount > 0 ? ConditionState::kTrue : ConditionState::kFalse;
        if (matchStart) {
            if (startedCount == 0) {
                mLastChangedToTrueDimensions.insert(outputKey);
                // This condition for this output key will change from false -> true
                changed = true;
            }

            // it's ok to do ++ here, even if we don't count nesting. The >1 counts will be treated
            // as 1 if not counting nesting.
            startedCount++;
            newCondition = ConditionState::kTrue;
        } else {
            // This is a stop event.
            if (startedCount > 0) {
                if (mCountNesting) {
                    startedCount--;
                    if (startedCount == 0) {
                        newCondition = ConditionState::kFalse;
                    }
                } else {
                    // not counting nesting, so ignore the number of starts, stop now.
                    startedCount = 0;
                    newCondition = ConditionState::kFalse;
                }
                // if everything has stopped for this output key, condition true -> false;
                if (startedCount == 0) {
                    mLastChangedToFalseDimensions.insert(outputKey);
                    changed = true;
                }
            }

            // if default condition is false, it means we don't need to keep the false values.
            if (mInitialValue == ConditionState::kFalse && startedCount == 0) {
                mSlicedConditionState.erase(outputIt);
                VLOG("erase key %s", outputKey.toString().c_str());
            }
        }
    }

    // dump all dimensions for debugging
    if (STATSD_DEBUG) {
        dumpState();
    }

    (*conditionChangedCache) = changed;
    (*conditionCache) = newCondition;

    VLOG("SimplePredicate %lld nonSlicedChange? %d", (long long)mConditionId,
         *conditionChangedCache);
}

void SimpleConditionTracker::evaluateCondition(const LogEvent& event,
                                               const vector<MatchingState>& eventMatcherValues,
                                               const vector<sp<ConditionTracker>>& mAllConditions,
                                               vector<ConditionState>& conditionCache,
                                               vector<uint8_t>& conditionChangedCache) {
    if (conditionCache[mIndex] != ConditionState::kNotEvaluated) {
        // it has been evaluated.
        VLOG("Yes, already evaluated, %lld %d",
            (long long)mConditionId, conditionCache[mIndex]);
        return;
    }
    mLastChangedToTrueDimensions.clear();
    mLastChangedToFalseDimensions.clear();

    if (mStopAllLogMatcherIndex >= 0 && mStopAllLogMatcherIndex < int(eventMatcherValues.size()) &&
        eventMatcherValues[mStopAllLogMatcherIndex] == MatchingState::kMatched) {
        handleStopAll(conditionCache, conditionChangedCache);
        return;
    }

    int matchedState = -1;
    // Note: The order to evaluate the following start, stop, stop_all matters.
    // The priority of overwrite is stop_all > stop > start.
    if (mStartLogMatcherIndex >= 0 &&
        eventMatcherValues[mStartLogMatcherIndex] == MatchingState::kMatched) {
        matchedState = 1;
    }

    if (mStopLogMatcherIndex >= 0 &&
        eventMatcherValues[mStopLogMatcherIndex] == MatchingState::kMatched) {
        matchedState = 0;
    }

    if (matchedState < 0) {
        // The event doesn't match this condition. So we just report existing condition values.
        conditionChangedCache[mIndex] = false;
        if (mSliced) {
            // if the condition result is sliced. The overall condition is true if any of the sliced
            // condition is true
            conditionCache[mIndex] = mInitialValue;
            for (const auto& slicedCondition : mSlicedConditionState) {
                if (slicedCondition.second > 0) {
                    conditionCache[mIndex] = ConditionState::kTrue;
                    break;
                }
            }
        } else {
            const auto& itr = mSlicedConditionState.find(DEFAULT_DIMENSION_KEY);
            if (itr == mSlicedConditionState.end()) {
                // condition not sliced, but we haven't seen the matched start or stop yet. so
                // return initial value.
                conditionCache[mIndex] = mInitialValue;
            } else {
                // return the cached condition.
                conditionCache[mIndex] =
                        itr->second > 0 ? ConditionState::kTrue : ConditionState::kFalse;
            }
        }
        return;
    }

    ConditionState overallState = mInitialValue;
    bool overallChanged = false;

    if (mOutputDimensions.size() == 0) {
        handleConditionEvent(DEFAULT_DIMENSION_KEY, matchedState == 1, &overallState,
                             &overallChanged);
    } else if (!mContainANYPositionInInternalDimensions) {
        HashableDimensionKey outputValue;
        filterValues(mOutputDimensions, event.getValues(), &outputValue);

        // If this event has multiple nodes in the attribution chain,  this log event probably will
        // generate multiple dimensions. If so, we will find if the condition changes for any
        // dimension and ask the corresponding metric producer to verify whether the actual sliced
        // condition has changed or not.
        // A high level assumption is that a predicate is either sliced or unsliced. We will never
        // have both sliced and unsliced version of a predicate.
        handleConditionEvent(outputValue, matchedState == 1, &overallState, &overallChanged);
    } else {
        ALOGE("The condition tracker should not be sliced by ANY position matcher.");
    }
    conditionCache[mIndex] = overallState;
    conditionChangedCache[mIndex] = overallChanged;
}

void SimpleConditionTracker::isConditionMet(
        const ConditionKey& conditionParameters, const vector<sp<ConditionTracker>>& allConditions,
        const bool isPartialLink,
        vector<ConditionState>& conditionCache) const {

    if (conditionCache[mIndex] != ConditionState::kNotEvaluated) {
        // it has been evaluated.
        VLOG("Yes, already evaluated, %lld %d",
            (long long)mConditionId, conditionCache[mIndex]);
        return;
    }
    const auto pair = conditionParameters.find(mConditionId);

    if (pair == conditionParameters.end()) {
        ConditionState conditionState = ConditionState::kNotEvaluated;
        conditionState = conditionState | mInitialValue;
        if (!mSliced) {
            const auto& itr = mSlicedConditionState.find(DEFAULT_DIMENSION_KEY);
            if (itr != mSlicedConditionState.end()) {
                ConditionState sliceState =
                    itr->second > 0 ? ConditionState::kTrue : ConditionState::kFalse;
                conditionState = conditionState | sliceState;
            }
        }
        conditionCache[mIndex] = conditionState;
        return;
    }

    ConditionState conditionState = ConditionState::kNotEvaluated;
    const HashableDimensionKey& key = pair->second;
    if (isPartialLink) {
        // For unseen key, check whether the require dimensions are subset of sliced condition
        // output.
        conditionState = conditionState | mInitialValue;
        for (const auto& slice : mSlicedConditionState) {
            ConditionState sliceState =
                slice.second > 0 ? ConditionState::kTrue : ConditionState::kFalse;
            if (slice.first.contains(key)) {
                conditionState = conditionState | sliceState;
            }
        }
    } else {
        auto startedCountIt = mSlicedConditionState.find(key);
        conditionState = conditionState | mInitialValue;
        if (startedCountIt != mSlicedConditionState.end()) {
            ConditionState sliceState =
                startedCountIt->second > 0 ? ConditionState::kTrue : ConditionState::kFalse;
            conditionState = conditionState | sliceState;
        }

    }
    conditionCache[mIndex] = conditionState;
    VLOG("Predicate %lld return %d", (long long)mConditionId, conditionCache[mIndex]);
}

}  // namespace statsd
}  // namespace os
}  // namespace android
