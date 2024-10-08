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

#include "AnomalyTracker.h"
#include "external/Perfetto.h"
#include "guardrail/StatsdStats.h"
#include "metadata_util.h"
#include "stats_log_util.h"
#include "subscriber_util.h"
#include "subscriber/IncidentdReporter.h"
#include "subscriber/SubscriberReporter.h"

#include <inttypes.h>
#include <statslog_statsd.h>
#include <time.h>

namespace android {
namespace os {
namespace statsd {

AnomalyTracker::AnomalyTracker(const Alert& alert, const ConfigKey& configKey)
        : mAlert(alert), mConfigKey(configKey), mNumOfPastBuckets(mAlert.num_buckets() - 1) {
    VLOG("AnomalyTracker() called");
    resetStorage();  // initialization
}

AnomalyTracker::~AnomalyTracker() {
    VLOG("~AnomalyTracker() called");
}

void AnomalyTracker::onConfigUpdated() {
    mSubscriptions.clear();
}

void AnomalyTracker::resetStorage() {
    VLOG("resetStorage() called.");
    mPastBuckets.clear();
    // Excludes the current bucket.
    mPastBuckets.resize(mNumOfPastBuckets);
    mSumOverPastBuckets.clear();
}

size_t AnomalyTracker::index(int64_t bucketNum) const {
    if (bucketNum < 0) {
        ALOGE("index() was passed a negative bucket number (%lld)!", (long long)bucketNum);
    }
    return bucketNum % mNumOfPastBuckets;
}

void AnomalyTracker::advanceMostRecentBucketTo(const int64_t bucketNum) {
    VLOG("advanceMostRecentBucketTo() called.");
    if (mNumOfPastBuckets <= 0) {
        return;
    }
    if (bucketNum <= mMostRecentBucketNum) {
        ALOGW("Cannot advance buckets backwards (bucketNum=%lld but mMostRecentBucketNum=%lld)",
              (long long)bucketNum, (long long)mMostRecentBucketNum);
        return;
    }
    // If in the future (i.e. buckets are ancient), just empty out all past info.
    if (bucketNum >= mMostRecentBucketNum + mNumOfPastBuckets) {
        resetStorage();
        mMostRecentBucketNum = bucketNum;
        return;
    }

    // Clear out space by emptying out old mPastBuckets[i] values and update mSumOverPastBuckets.
    for (int64_t i = mMostRecentBucketNum + 1; i <= bucketNum; i++) {
        const int idx = index(i);
        subtractBucketFromSum(mPastBuckets[idx]);
        mPastBuckets[idx] = nullptr;  // release (but not clear) the old bucket.
    }
    mMostRecentBucketNum = bucketNum;
}

void AnomalyTracker::addPastBucket(const MetricDimensionKey& key, const int64_t bucketValue,
                                   const int64_t bucketNum) {
    VLOG("addPastBucket(bucketValue) called.");
    if (mNumOfPastBuckets == 0 ||
        bucketNum < 0 || bucketNum <= mMostRecentBucketNum - mNumOfPastBuckets) {
        return;
    }

    const int bucketIndex = index(bucketNum);
    if (bucketNum <= mMostRecentBucketNum && (mPastBuckets[bucketIndex] != nullptr)) {
        // We need to insert into an already existing past bucket.
        std::shared_ptr<DimToValMap>& bucket = mPastBuckets[bucketIndex];
        auto itr = bucket->find(key);
        if (itr != bucket->end()) {
            // Old entry already exists; update it.
            subtractValueFromSum(key, itr->second);
            itr->second = bucketValue;
        } else {
            bucket->insert({key, bucketValue});
        }
        mSumOverPastBuckets[key] += bucketValue;
    } else {
        // Bucket does not exist yet (in future or was never made), so we must make it.
        std::shared_ptr<DimToValMap> bucket = std::make_shared<DimToValMap>();
        bucket->insert({key, bucketValue});
        addPastBucket(bucket, bucketNum);
    }
}

void AnomalyTracker::addPastBucket(const std::shared_ptr<DimToValMap>& bucket,
                                   const int64_t bucketNum) {
    VLOG("addPastBucket(bucket) called.");
    if (mNumOfPastBuckets == 0 ||
            bucketNum < 0 || bucketNum <= mMostRecentBucketNum - mNumOfPastBuckets) {
        return;
    }

    if (bucketNum <= mMostRecentBucketNum) {
        // We are updating an old bucket, not adding a new one.
        subtractBucketFromSum(mPastBuckets[index(bucketNum)]);
    } else {
        // Clear space for the new bucket to be at bucketNum.
        advanceMostRecentBucketTo(bucketNum);
    }
    mPastBuckets[index(bucketNum)] = bucket;
    addBucketToSum(bucket);
}

void AnomalyTracker::subtractBucketFromSum(const shared_ptr<DimToValMap>& bucket) {
    if (bucket == nullptr) {
        return;
    }
    for (const auto& keyValuePair : *bucket) {
        subtractValueFromSum(keyValuePair.first, keyValuePair.second);
    }
}

void AnomalyTracker::subtractValueFromSum(const MetricDimensionKey& key,
                                          const int64_t bucketValue) {
    auto itr = mSumOverPastBuckets.find(key);
    if (itr == mSumOverPastBuckets.end()) {
        return;
    }
    itr->second -= bucketValue;
    if (itr->second == 0) {
        mSumOverPastBuckets.erase(itr);
    }
}

void AnomalyTracker::addBucketToSum(const shared_ptr<DimToValMap>& bucket) {
    if (bucket == nullptr) {
        return;
    }
    // For each dimension present in the bucket, add its value to its corresponding sum.
    for (const auto& keyValuePair : *bucket) {
        mSumOverPastBuckets[keyValuePair.first] += keyValuePair.second;
    }
}

int64_t AnomalyTracker::getPastBucketValue(const MetricDimensionKey& key,
                                           const int64_t bucketNum) const {
    if (bucketNum < 0 || mMostRecentBucketNum < 0
            || bucketNum <= mMostRecentBucketNum - mNumOfPastBuckets
            || bucketNum > mMostRecentBucketNum) {
        return 0;
    }

    const auto& bucket = mPastBuckets[index(bucketNum)];
    if (bucket == nullptr) {
        return 0;
    }
    const auto& itr = bucket->find(key);
    return itr == bucket->end() ? 0 : itr->second;
}

int64_t AnomalyTracker::getSumOverPastBuckets(const MetricDimensionKey& key) const {
    const auto& itr = mSumOverPastBuckets.find(key);
    if (itr != mSumOverPastBuckets.end()) {
        return itr->second;
    }
    return 0;
}

bool AnomalyTracker::detectAnomaly(const int64_t currentBucketNum, const MetricDimensionKey& key,
                                   const int64_t currentBucketValue) {
    // currentBucketNum should be the next bucket after pastBuckets. If not, advance so that it is.
    if (currentBucketNum > mMostRecentBucketNum + 1) {
        advanceMostRecentBucketTo(currentBucketNum - 1);
    }
    return mAlert.has_trigger_if_sum_gt() &&
           getSumOverPastBuckets(key) + currentBucketValue > mAlert.trigger_if_sum_gt();
}

void AnomalyTracker::declareAnomaly(const int64_t timestampNs, int64_t metricId,
                                    const MetricDimensionKey& key, int64_t metricValue) {
    // TODO(b/110563466): Why receive timestamp? RefractoryPeriod should always be based on
    // real time right now.
    if (isInRefractoryPeriod(timestampNs, key)) {
        VLOG("Skipping anomaly declaration since within refractory period");
        return;
    }

    // TODO(b/110564268): This should also take in the const MetricDimensionKey& key?
    util::stats_write(util::ANOMALY_DETECTED, mConfigKey.GetUid(), mConfigKey.GetId(), mAlert.id());

    if (mAlert.probability_of_informing() < 1 &&
        ((float)rand() / (float)RAND_MAX) >= mAlert.probability_of_informing()) {
        // Note that due to float imprecision, 0.0 and 1.0 might not truly mean never/always.
        // The config writer was advised to use -0.1 and 1.1 for never/always.
        ALOGI("Fate decided that an alert will not trigger subscribers or start the refactory "
              "period countdown.");
        return;
    }

    if (mAlert.has_refractory_period_secs()) {
        mRefractoryPeriodEndsSec[key] = ((timestampNs + NS_PER_SEC - 1) / NS_PER_SEC) // round up
                                        + mAlert.refractory_period_secs();
        // TODO(b/110563466): If we had access to the bucket_size_millis, consider
        // calling resetStorage()
        // if (mAlert.refractory_period_secs() > mNumOfPastBuckets * bucketSizeNs) {resetStorage();}
    }

    if (!mSubscriptions.empty()) {
        ALOGI("An anomaly (%" PRId64 ") %s has occurred! Informing subscribers.",
                mAlert.id(), key.toString().c_str());
        informSubscribers(key, metricId, metricValue);
    } else {
        ALOGI("An anomaly has occurred! (But no subscriber for that alert.)");
    }

    StatsdStats::getInstance().noteAnomalyDeclared(mConfigKey, mAlert.id());
}

void AnomalyTracker::detectAndDeclareAnomaly(const int64_t timestampNs, const int64_t currBucketNum,
                                             int64_t metricId, const MetricDimensionKey& key,
                                             const int64_t currentBucketValue) {
    if (detectAnomaly(currBucketNum, key, currentBucketValue)) {
        declareAnomaly(timestampNs, metricId, key, currentBucketValue);
    }
}

bool AnomalyTracker::isInRefractoryPeriod(const int64_t timestampNs,
                                          const MetricDimensionKey& key) const {
    const auto& it = mRefractoryPeriodEndsSec.find(key);
    if (it != mRefractoryPeriodEndsSec.end()) {
        return timestampNs < (it->second *  (int64_t)NS_PER_SEC);
    }
    return false;
}

std::pair<optional<InvalidConfigReason>, uint64_t> AnomalyTracker::getProtoHash() const {
    string serializedAlert;
    if (!mAlert.SerializeToString(&serializedAlert)) {
        ALOGW("Unable to serialize alert %lld", (long long)mAlert.id());
        return {createInvalidConfigReasonWithAlert(INVALID_CONFIG_REASON_ALERT_SERIALIZATION_FAILED,
                                                   mAlert.metric_id(), mAlert.id()),
                0};
    }
    return {nullopt, Hash64(serializedAlert)};
}

void AnomalyTracker::informSubscribers(const MetricDimensionKey& key, int64_t metric_id,
                                       int64_t metricValue) {
    triggerSubscribers(mAlert.id(), metric_id, key, metricValue, mConfigKey, mSubscriptions);
}

bool AnomalyTracker::writeAlertMetadataToProto(int64_t currentWallClockTimeNs,
                                               int64_t systemElapsedTimeNs,
                                               metadata::AlertMetadata* alertMetadata) {
    bool metadataWritten = false;

    if (mRefractoryPeriodEndsSec.empty()) {
        return false;
    }

    for (const auto& it: mRefractoryPeriodEndsSec) {
        // Do not write the timestamp to disk if it has already expired
        if (it.second < systemElapsedTimeNs / NS_PER_SEC) {
            continue;
        }

        metadataWritten = true;
        if (alertMetadata->alert_dim_keyed_data_size() == 0) {
            alertMetadata->set_alert_id(mAlert.id());
        }

        metadata::AlertDimensionKeyedData* keyedData = alertMetadata->add_alert_dim_keyed_data();
        // We convert and write the refractory_end_sec to wall clock time because we do not know
        // when statsd will start again.
        int32_t refractoryEndWallClockSec = (int32_t) ((currentWallClockTimeNs / NS_PER_SEC) +
                (it.second - systemElapsedTimeNs / NS_PER_SEC));

        keyedData->set_last_refractory_ends_sec(refractoryEndWallClockSec);
        writeMetricDimensionKeyToMetadataDimensionKey(
                it.first, keyedData->mutable_dimension_key());
    }

    return metadataWritten;
}

void AnomalyTracker::loadAlertMetadata(
        const metadata::AlertMetadata& alertMetadata,
        int64_t currentWallClockTimeNs,
        int64_t systemElapsedTimeNs) {
    for (const metadata::AlertDimensionKeyedData& keyedData :
            alertMetadata.alert_dim_keyed_data()) {
        if ((uint64_t) keyedData.last_refractory_ends_sec() < currentWallClockTimeNs / NS_PER_SEC) {
            // Do not update the timestamp if it has already expired.
            continue;
        }
        MetricDimensionKey metricKey = loadMetricDimensionKeyFromProto(
                keyedData.dimension_key());
        int32_t refractoryPeriodEndsSec = (int32_t) keyedData.last_refractory_ends_sec() -
                currentWallClockTimeNs / NS_PER_SEC + systemElapsedTimeNs / NS_PER_SEC;
        mRefractoryPeriodEndsSec[metricKey] = refractoryPeriodEndsSec;
    }
}

}  // namespace statsd
}  // namespace os
}  // namespace android
