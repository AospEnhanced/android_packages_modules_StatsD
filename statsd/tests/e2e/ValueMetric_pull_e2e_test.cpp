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

#include <android/binder_interface_utils.h>
#include <gtest/gtest.h>

#include <vector>

#include "src/StatsLogProcessor.h"
#include "src/stats_log_util.h"
#include "tests/statsd_test_util.h"

using ::ndk::SharedRefBase;

namespace android {
namespace os {
namespace statsd {

#ifdef __ANDROID__

namespace {

const int64_t metricId = 123456;

StatsdConfig CreateStatsdConfig(bool useCondition = true) {
    StatsdConfig config;
    config.add_default_pull_packages("AID_ROOT");  // Fake puller is registered with root.
    auto pulledAtomMatcher =
            CreateSimpleAtomMatcher("TestMatcher", util::SUBSYSTEM_SLEEP_STATE);
    *config.add_atom_matcher() = pulledAtomMatcher;
    *config.add_atom_matcher() = CreateScreenTurnedOnAtomMatcher();
    *config.add_atom_matcher() = CreateScreenTurnedOffAtomMatcher();

    auto screenIsOffPredicate = CreateScreenIsOffPredicate();
    *config.add_predicate() = screenIsOffPredicate;

    auto valueMetric = config.add_value_metric();
    valueMetric->set_id(metricId);
    valueMetric->set_what(pulledAtomMatcher.id());
    if (useCondition) {
        valueMetric->set_condition(screenIsOffPredicate.id());
    }
    *valueMetric->mutable_value_field() =
            CreateDimensions(util::SUBSYSTEM_SLEEP_STATE, {4 /* time sleeping field */});
    *valueMetric->mutable_dimensions_in_what() =
            CreateDimensions(util::SUBSYSTEM_SLEEP_STATE, {1 /* subsystem name */});
    valueMetric->set_bucket(FIVE_MINUTES);
    valueMetric->set_use_absolute_value_on_reset(true);
    valueMetric->set_skip_zero_diff_output(false);
    valueMetric->set_max_pull_delay_sec(INT_MAX);
    valueMetric->set_split_bucket_for_app_upgrade(true);
    valueMetric->set_min_bucket_size_nanos(1000);
    return config;
}

StatsdConfig CreateStatsdConfigWithStates() {
    StatsdConfig config;
    config.add_default_pull_packages("AID_ROOT");  // Fake puller is registered with root.

    auto pulledAtomMatcher = CreateSimpleAtomMatcher("TestMatcher", util::SUBSYSTEM_SLEEP_STATE);
    *config.add_atom_matcher() = pulledAtomMatcher;
    *config.add_atom_matcher() = CreateScreenTurnedOnAtomMatcher();
    *config.add_atom_matcher() = CreateScreenTurnedOffAtomMatcher();
    *config.add_atom_matcher() = CreateBatteryStateNoneMatcher();
    *config.add_atom_matcher() = CreateBatteryStateUsbMatcher();

    auto screenOnPredicate = CreateScreenIsOnPredicate();
    *config.add_predicate() = screenOnPredicate;

    auto screenOffPredicate = CreateScreenIsOffPredicate();
    *config.add_predicate() = screenOffPredicate;

    auto deviceUnpluggedPredicate = CreateDeviceUnpluggedPredicate();
    *config.add_predicate() = deviceUnpluggedPredicate;

    auto screenOnOnBatteryPredicate = config.add_predicate();
    screenOnOnBatteryPredicate->set_id(StringToId("screenOnOnBatteryPredicate"));
    screenOnOnBatteryPredicate->mutable_combination()->set_operation(LogicalOperation::AND);
    addPredicateToPredicateCombination(screenOnPredicate, screenOnOnBatteryPredicate);
    addPredicateToPredicateCombination(deviceUnpluggedPredicate, screenOnOnBatteryPredicate);

    auto screenOffOnBatteryPredicate = config.add_predicate();
    screenOffOnBatteryPredicate->set_id(StringToId("ScreenOffOnBattery"));
    screenOffOnBatteryPredicate->mutable_combination()->set_operation(LogicalOperation::AND);
    addPredicateToPredicateCombination(screenOffPredicate, screenOffOnBatteryPredicate);
    addPredicateToPredicateCombination(deviceUnpluggedPredicate, screenOffOnBatteryPredicate);

    const State screenState =
            CreateScreenStateWithSimpleOnOffMap(/*screen on id=*/321, /*screen off id=*/123);
    *config.add_state() = screenState;

    // ValueMetricSubsystemSleepWhileScreenOnOnBattery
    auto valueMetric1 = config.add_value_metric();
    valueMetric1->set_id(metricId);
    valueMetric1->set_what(pulledAtomMatcher.id());
    valueMetric1->set_condition(screenOnOnBatteryPredicate->id());
    *valueMetric1->mutable_value_field() =
            CreateDimensions(util::SUBSYSTEM_SLEEP_STATE, {4 /* time sleeping field */});
    valueMetric1->set_bucket(FIVE_MINUTES);
    valueMetric1->set_use_absolute_value_on_reset(true);
    valueMetric1->set_skip_zero_diff_output(false);
    valueMetric1->set_max_pull_delay_sec(INT_MAX);

    // ValueMetricSubsystemSleepWhileScreenOffOnBattery
    ValueMetric* valueMetric2 = config.add_value_metric();
    valueMetric2->set_id(StringToId("ValueMetricSubsystemSleepWhileScreenOffOnBattery"));
    valueMetric2->set_what(pulledAtomMatcher.id());
    valueMetric2->set_condition(screenOffOnBatteryPredicate->id());
    *valueMetric2->mutable_value_field() =
            CreateDimensions(util::SUBSYSTEM_SLEEP_STATE, {4 /* time sleeping field */});
    valueMetric2->set_bucket(FIVE_MINUTES);
    valueMetric2->set_use_absolute_value_on_reset(true);
    valueMetric2->set_skip_zero_diff_output(false);
    valueMetric2->set_max_pull_delay_sec(INT_MAX);

    // ValueMetricSubsystemSleepWhileOnBatterySliceScreen
    ValueMetric* valueMetric3 = config.add_value_metric();
    valueMetric3->set_id(StringToId("ValueMetricSubsystemSleepWhileOnBatterySliceScreen"));
    valueMetric3->set_what(pulledAtomMatcher.id());
    valueMetric3->set_condition(deviceUnpluggedPredicate.id());
    *valueMetric3->mutable_value_field() =
            CreateDimensions(util::SUBSYSTEM_SLEEP_STATE, {4 /* time sleeping field */});
    valueMetric3->add_slice_by_state(screenState.id());
    valueMetric3->set_bucket(FIVE_MINUTES);
    valueMetric3->set_use_absolute_value_on_reset(true);
    valueMetric3->set_skip_zero_diff_output(false);
    valueMetric3->set_max_pull_delay_sec(INT_MAX);
    return config;
}

}  // namespace

/**
 * Tests the initial condition and condition after the first log events for
 * value metrics with either a combination condition or simple condition.
 *
 * Metrics should be initialized with condition kUnknown (given that the
 * predicate is using the default InitialValue of UNKNOWN). The condition should
 * be updated to either kFalse or kTrue if a condition event is logged for all
 * children conditions.
 */
TEST(ValueMetricE2eTest, TestInitialConditionChanges) {
    StatsdConfig config = CreateStatsdConfigWithStates();
    int64_t baseTimeNs = getElapsedRealtimeNs();
    int64_t configAddedTimeNs = 10 * 60 * NS_PER_SEC + baseTimeNs;
    int64_t bucketSizeNs = TimeUnitToBucketSizeInMillis(config.value_metric(0).bucket()) * 1000000;

    ConfigKey cfgKey;
    int32_t tagId = util::SUBSYSTEM_SLEEP_STATE;
    auto processor =
            CreateStatsLogProcessor(baseTimeNs, configAddedTimeNs, config, cfgKey,
                                    SharedRefBase::make<FakeSubsystemSleepCallback>(), tagId);

    EXPECT_EQ(processor->mMetricsManagers.size(), 1u);
    sp<MetricsManager> metricsManager = processor->mMetricsManagers.begin()->second;
    EXPECT_TRUE(metricsManager->isConfigValid());
    EXPECT_EQ(3, metricsManager->mAllMetricProducers.size());

    // Combination condition metric - screen on and device unplugged
    sp<MetricProducer> metricProducer1 = metricsManager->mAllMetricProducers[0];
    // Simple condition metric - device unplugged
    sp<MetricProducer> metricProducer2 = metricsManager->mAllMetricProducers[2];

    EXPECT_EQ(ConditionState::kUnknown, metricProducer1->mCondition);
    EXPECT_EQ(ConditionState::kUnknown, metricProducer2->mCondition);

    auto screenOnEvent =
            CreateScreenStateChangedEvent(configAddedTimeNs + 30, android::view::DISPLAY_STATE_ON);
    processor->OnLogEvent(screenOnEvent.get());
    EXPECT_EQ(ConditionState::kUnknown, metricProducer1->mCondition);
    EXPECT_EQ(ConditionState::kUnknown, metricProducer2->mCondition);

    auto screenOffEvent =
            CreateScreenStateChangedEvent(configAddedTimeNs + 40, android::view::DISPLAY_STATE_OFF);
    processor->OnLogEvent(screenOffEvent.get());
    EXPECT_EQ(ConditionState::kUnknown, metricProducer1->mCondition);
    EXPECT_EQ(ConditionState::kUnknown, metricProducer2->mCondition);

    auto pluggedUsbEvent = CreateBatteryStateChangedEvent(
            configAddedTimeNs + 50, BatteryPluggedStateEnum::BATTERY_PLUGGED_USB);
    processor->OnLogEvent(pluggedUsbEvent.get());
    EXPECT_EQ(ConditionState::kFalse, metricProducer1->mCondition);
    EXPECT_EQ(ConditionState::kFalse, metricProducer2->mCondition);

    auto pluggedNoneEvent = CreateBatteryStateChangedEvent(
            configAddedTimeNs + 70, BatteryPluggedStateEnum::BATTERY_PLUGGED_NONE);
    processor->OnLogEvent(pluggedNoneEvent.get());
    EXPECT_EQ(ConditionState::kFalse, metricProducer1->mCondition);
    EXPECT_EQ(ConditionState::kTrue, metricProducer2->mCondition);
}

TEST(ValueMetricE2eTest, TestPulledEvents) {
    auto config = CreateStatsdConfig();
    int64_t baseTimeNs = getElapsedRealtimeNs();
    int64_t configAddedTimeNs = 10 * 60 * NS_PER_SEC + baseTimeNs;
    int64_t bucketSizeNs = TimeUnitToBucketSizeInMillis(config.value_metric(0).bucket()) * 1000000;

    ConfigKey cfgKey;
    auto processor = CreateStatsLogProcessor(baseTimeNs, configAddedTimeNs, config, cfgKey,
                                             SharedRefBase::make<FakeSubsystemSleepCallback>(),
                                             util::SUBSYSTEM_SLEEP_STATE);
    ASSERT_EQ(processor->mMetricsManagers.size(), 1u);
    EXPECT_TRUE(processor->mMetricsManagers.begin()->second->isConfigValid());
    processor->mPullerManager->ForceClearPullerCache();

    int startBucketNum = processor->mMetricsManagers.begin()
                                 ->second->mAllMetricProducers[0]
                                 ->getCurrentBucketNum();
    EXPECT_GT(startBucketNum, (int64_t)0);

    // When creating the config, the value metric producer should register the alarm at the
    // end of the current bucket.
    ASSERT_EQ((size_t)1, processor->mPullerManager->mReceivers.size());
    EXPECT_EQ(bucketSizeNs,
              processor->mPullerManager->mReceivers.begin()->second.front().intervalNs);
    int64_t& expectedPullTimeNs =
            processor->mPullerManager->mReceivers.begin()->second.front().nextPullTimeNs;
    EXPECT_EQ(baseTimeNs + startBucketNum * bucketSizeNs + bucketSizeNs, expectedPullTimeNs);

    auto screenOffEvent =
            CreateScreenStateChangedEvent(configAddedTimeNs + 55, android::view::DISPLAY_STATE_OFF);
    processor->OnLogEvent(screenOffEvent.get());

    auto screenOnEvent =
            CreateScreenStateChangedEvent(configAddedTimeNs + 65, android::view::DISPLAY_STATE_ON);
    processor->OnLogEvent(screenOnEvent.get());

    screenOffEvent =
            CreateScreenStateChangedEvent(configAddedTimeNs + 75, android::view::DISPLAY_STATE_OFF);
    processor->OnLogEvent(screenOffEvent.get());

    // Pulling alarm arrives on time and reset the sequential pulling alarm.
    processor->informPullAlarmFired(expectedPullTimeNs + 1);
    EXPECT_EQ(baseTimeNs + startBucketNum * bucketSizeNs + 2 * bucketSizeNs, expectedPullTimeNs);

    processor->informPullAlarmFired(expectedPullTimeNs + 1);

    screenOnEvent = CreateScreenStateChangedEvent(configAddedTimeNs + 2 * bucketSizeNs + 15,
                                                  android::view::DISPLAY_STATE_ON);
    processor->OnLogEvent(screenOnEvent.get());

    processor->informPullAlarmFired(expectedPullTimeNs + 1);

    processor->informPullAlarmFired(expectedPullTimeNs + 1);

    screenOffEvent = CreateScreenStateChangedEvent(configAddedTimeNs + 4 * bucketSizeNs + 11,
                                                   android::view::DISPLAY_STATE_OFF);
    processor->OnLogEvent(screenOffEvent.get());

    processor->informPullAlarmFired(expectedPullTimeNs + 1);

    processor->informPullAlarmFired(expectedPullTimeNs + 1);

    ConfigMetricsReportList reports;
    vector<uint8_t> buffer;
    processor->onDumpReport(cfgKey, configAddedTimeNs + 7 * bucketSizeNs + 10, false, true,
                            ADB_DUMP, FAST, &buffer);
    EXPECT_TRUE(buffer.size() > 0);
    EXPECT_TRUE(reports.ParseFromArray(&buffer[0], buffer.size()));
    backfillDimensionPath(&reports);
    backfillStringInReport(&reports);
    backfillStartEndTimestamp(&reports);
    ASSERT_EQ(1, reports.reports_size());
    ASSERT_EQ(1, reports.reports(0).metrics_size());
    StatsLogReport::ValueMetricDataWrapper valueMetrics;
    EXPECT_TRUE(reports.reports(0).metrics(0).has_estimated_data_bytes());
    sortMetricDataByDimensionsValue(reports.reports(0).metrics(0).value_metrics(), &valueMetrics);
    ASSERT_GT((int)valueMetrics.data_size(), 1);

    auto data = valueMetrics.data(0);
    EXPECT_EQ(util::SUBSYSTEM_SLEEP_STATE, data.dimensions_in_what().field());
    ASSERT_EQ(1, data.dimensions_in_what().value_tuple().dimensions_value_size());
    EXPECT_EQ(1 /* subsystem name field */,
              data.dimensions_in_what().value_tuple().dimensions_value(0).field());
    EXPECT_FALSE(data.dimensions_in_what().value_tuple().dimensions_value(0).value_str().empty());
    // We have 4 buckets, the first one was incomplete since the condition was unknown.
    ASSERT_EQ(4, data.bucket_info_size());

    EXPECT_EQ(baseTimeNs + 3 * bucketSizeNs, data.bucket_info(0).start_bucket_elapsed_nanos());
    EXPECT_EQ(baseTimeNs + 4 * bucketSizeNs, data.bucket_info(0).end_bucket_elapsed_nanos());
    ASSERT_EQ(1, data.bucket_info(0).values_size());

    EXPECT_EQ(baseTimeNs + 4 * bucketSizeNs, data.bucket_info(1).start_bucket_elapsed_nanos());
    EXPECT_EQ(baseTimeNs + 5 * bucketSizeNs, data.bucket_info(1).end_bucket_elapsed_nanos());
    ASSERT_EQ(1, data.bucket_info(1).values_size());

    EXPECT_EQ(baseTimeNs + 6 * bucketSizeNs, data.bucket_info(2).start_bucket_elapsed_nanos());
    EXPECT_EQ(baseTimeNs + 7 * bucketSizeNs, data.bucket_info(2).end_bucket_elapsed_nanos());
    ASSERT_EQ(1, data.bucket_info(2).values_size());

    EXPECT_EQ(baseTimeNs + 7 * bucketSizeNs, data.bucket_info(3).start_bucket_elapsed_nanos());
    EXPECT_EQ(baseTimeNs + 8 * bucketSizeNs, data.bucket_info(3).end_bucket_elapsed_nanos());
    ASSERT_EQ(1, data.bucket_info(3).values_size());

    valueMetrics = reports.reports(0).metrics(0).value_metrics();
    ASSERT_EQ(2, valueMetrics.skipped_size());

    StatsLogReport::SkippedBuckets skipped = valueMetrics.skipped(0);
    EXPECT_EQ(BucketDropReason::CONDITION_UNKNOWN, skipped.drop_event(0).drop_reason());
    EXPECT_EQ(MillisToNano(NanoToMillis(baseTimeNs + 2 * bucketSizeNs)),
              skipped.start_bucket_elapsed_nanos());
    EXPECT_EQ(MillisToNano(NanoToMillis(baseTimeNs + 3 * bucketSizeNs)),
              skipped.end_bucket_elapsed_nanos());

    skipped = valueMetrics.skipped(1);
    EXPECT_EQ(BucketDropReason::NO_DATA, skipped.drop_event(0).drop_reason());
    EXPECT_EQ(MillisToNano(NanoToMillis(baseTimeNs + 5 * bucketSizeNs)),
              skipped.start_bucket_elapsed_nanos());
    EXPECT_EQ(MillisToNano(NanoToMillis(baseTimeNs + 6 * bucketSizeNs)),
              skipped.end_bucket_elapsed_nanos());
}

TEST(ValueMetricE2eTest, TestPulledEvents_LateAlarm) {
    auto config = CreateStatsdConfig();
    int64_t baseTimeNs = getElapsedRealtimeNs();
    // 10 mins == 2 bucket durations.
    int64_t configAddedTimeNs = 10 * 60 * NS_PER_SEC + baseTimeNs;
    int64_t bucketSizeNs = TimeUnitToBucketSizeInMillis(config.value_metric(0).bucket()) * 1000000;

    ConfigKey cfgKey;
    auto processor = CreateStatsLogProcessor(baseTimeNs, configAddedTimeNs, config, cfgKey,
                                             SharedRefBase::make<FakeSubsystemSleepCallback>(),
                                             util::SUBSYSTEM_SLEEP_STATE);
    ASSERT_EQ(processor->mMetricsManagers.size(), 1u);
    EXPECT_TRUE(processor->mMetricsManagers.begin()->second->isConfigValid());
    processor->mPullerManager->ForceClearPullerCache();

    int startBucketNum = processor->mMetricsManagers.begin()
                                 ->second->mAllMetricProducers[0]
                                 ->getCurrentBucketNum();
    EXPECT_GT(startBucketNum, (int64_t)0);

    // When creating the config, the value metric producer should register the alarm at the
    // end of the current bucket.
    ASSERT_EQ((size_t)1, processor->mPullerManager->mReceivers.size());
    EXPECT_EQ(bucketSizeNs,
              processor->mPullerManager->mReceivers.begin()->second.front().intervalNs);
    int64_t& expectedPullTimeNs =
            processor->mPullerManager->mReceivers.begin()->second.front().nextPullTimeNs;
    EXPECT_EQ(baseTimeNs + startBucketNum * bucketSizeNs + bucketSizeNs, expectedPullTimeNs);

    // Screen off/on/off events.
    auto screenOffEvent =
            CreateScreenStateChangedEvent(configAddedTimeNs + 55, android::view::DISPLAY_STATE_OFF);
    processor->OnLogEvent(screenOffEvent.get());

    auto screenOnEvent =
            CreateScreenStateChangedEvent(configAddedTimeNs + 65, android::view::DISPLAY_STATE_ON);
    processor->OnLogEvent(screenOnEvent.get());

    screenOffEvent =
            CreateScreenStateChangedEvent(configAddedTimeNs + 75, android::view::DISPLAY_STATE_OFF);
    processor->OnLogEvent(screenOffEvent.get());

    // Pulling alarm arrives late by 2 buckets and 1 ns. 2 buckets late is too far away in the
    // future, data will be skipped.
    processor->informPullAlarmFired(expectedPullTimeNs + 2 * bucketSizeNs + 1);
    EXPECT_EQ(baseTimeNs + startBucketNum * bucketSizeNs + 4 * bucketSizeNs, expectedPullTimeNs);

    // This screen state change will start a new bucket.
    screenOnEvent = CreateScreenStateChangedEvent(configAddedTimeNs + 4 * bucketSizeNs + 65,
                                                  android::view::DISPLAY_STATE_ON);
    processor->OnLogEvent(screenOnEvent.get());

    // The alarm is delayed but we already created a bucket thanks to the screen state condition.
    // This bucket does not have to be skipped since the alarm arrives in time for the next bucket.
    processor->informPullAlarmFired(expectedPullTimeNs + bucketSizeNs + 21);
    EXPECT_EQ(baseTimeNs + startBucketNum * bucketSizeNs + 6 * bucketSizeNs, expectedPullTimeNs);

    screenOffEvent = CreateScreenStateChangedEvent(configAddedTimeNs + 6 * bucketSizeNs + 31,
                                                   android::view::DISPLAY_STATE_OFF);
    processor->OnLogEvent(screenOffEvent.get());

    processor->informPullAlarmFired(expectedPullTimeNs + bucketSizeNs + 21);
    EXPECT_EQ(baseTimeNs + startBucketNum * bucketSizeNs + 8 * bucketSizeNs, expectedPullTimeNs);

    processor->informPullAlarmFired(expectedPullTimeNs + 1);
    EXPECT_EQ(baseTimeNs + startBucketNum * bucketSizeNs + 9 * bucketSizeNs, expectedPullTimeNs);

    ConfigMetricsReportList reports;
    vector<uint8_t> buffer;
    processor->onDumpReport(cfgKey, configAddedTimeNs + 9 * bucketSizeNs + 10, false, true,
                            ADB_DUMP, FAST, &buffer);
    EXPECT_TRUE(buffer.size() > 0);
    EXPECT_TRUE(reports.ParseFromArray(&buffer[0], buffer.size()));
    backfillDimensionPath(&reports);
    backfillStringInReport(&reports);
    backfillStartEndTimestamp(&reports);
    ASSERT_EQ(1, reports.reports_size());
    ASSERT_EQ(1, reports.reports(0).metrics_size());
    StatsLogReport::ValueMetricDataWrapper valueMetrics;
    sortMetricDataByDimensionsValue(reports.reports(0).metrics(0).value_metrics(), &valueMetrics);
    ASSERT_GT((int)valueMetrics.data_size(), 1);

    auto data = valueMetrics.data(0);
    EXPECT_EQ(util::SUBSYSTEM_SLEEP_STATE, data.dimensions_in_what().field());
    ASSERT_EQ(1, data.dimensions_in_what().value_tuple().dimensions_value_size());
    EXPECT_EQ(1 /* subsystem name field */,
              data.dimensions_in_what().value_tuple().dimensions_value(0).field());
    EXPECT_FALSE(data.dimensions_in_what().value_tuple().dimensions_value(0).value_str().empty());
    ASSERT_EQ(3, data.bucket_info_size());

    EXPECT_EQ(baseTimeNs + 5 * bucketSizeNs, data.bucket_info(0).start_bucket_elapsed_nanos());
    EXPECT_EQ(baseTimeNs + 6 * bucketSizeNs, data.bucket_info(0).end_bucket_elapsed_nanos());
    ASSERT_EQ(1, data.bucket_info(0).values_size());

    EXPECT_EQ(baseTimeNs + 8 * bucketSizeNs, data.bucket_info(1).start_bucket_elapsed_nanos());
    EXPECT_EQ(baseTimeNs + 9 * bucketSizeNs, data.bucket_info(1).end_bucket_elapsed_nanos());
    ASSERT_EQ(1, data.bucket_info(1).values_size());

    EXPECT_EQ(baseTimeNs + 9 * bucketSizeNs, data.bucket_info(2).start_bucket_elapsed_nanos());
    EXPECT_EQ(baseTimeNs + 10 * bucketSizeNs, data.bucket_info(2).end_bucket_elapsed_nanos());
    ASSERT_EQ(1, data.bucket_info(2).values_size());

    valueMetrics = reports.reports(0).metrics(0).value_metrics();
    ASSERT_EQ(3, valueMetrics.skipped_size());

    StatsLogReport::SkippedBuckets skipped = valueMetrics.skipped(0);
    EXPECT_EQ(BucketDropReason::CONDITION_UNKNOWN, skipped.drop_event(0).drop_reason());
    EXPECT_EQ(MillisToNano(NanoToMillis(baseTimeNs + 2 * bucketSizeNs)),
              skipped.start_bucket_elapsed_nanos());
    EXPECT_EQ(MillisToNano(NanoToMillis(baseTimeNs + 5 * bucketSizeNs)),
              skipped.end_bucket_elapsed_nanos());

    skipped = valueMetrics.skipped(1);
    EXPECT_EQ(BucketDropReason::NO_DATA, skipped.drop_event(0).drop_reason());
    EXPECT_EQ(MillisToNano(NanoToMillis(baseTimeNs + 6 * bucketSizeNs)),
              skipped.start_bucket_elapsed_nanos());
    EXPECT_EQ(MillisToNano(NanoToMillis(baseTimeNs + 7 * bucketSizeNs)),
              skipped.end_bucket_elapsed_nanos());

    skipped = valueMetrics.skipped(2);
    EXPECT_EQ(BucketDropReason::NO_DATA, skipped.drop_event(0).drop_reason());
    EXPECT_EQ(MillisToNano(NanoToMillis(baseTimeNs + 7 * bucketSizeNs)),
              skipped.start_bucket_elapsed_nanos());
    EXPECT_EQ(MillisToNano(NanoToMillis(baseTimeNs + 8 * bucketSizeNs)),
              skipped.end_bucket_elapsed_nanos());
}

TEST(ValueMetricE2eTest, TestPulledEvents_WithActivation) {
    auto config = CreateStatsdConfig(false);
    int64_t baseTimeNs = getElapsedRealtimeNs();
    int64_t configAddedTimeNs = 10 * 60 * NS_PER_SEC + baseTimeNs;
    int64_t bucketSizeNs = TimeUnitToBucketSizeInMillis(config.value_metric(0).bucket()) * 1000000;

    auto batterySaverStartMatcher = CreateBatterySaverModeStartAtomMatcher();
    *config.add_atom_matcher() = batterySaverStartMatcher;
    const int64_t ttlNs = 2 * bucketSizeNs;  // Two buckets.
    auto metric_activation = config.add_metric_activation();
    metric_activation->set_metric_id(metricId);
    metric_activation->set_activation_type(ACTIVATE_IMMEDIATELY);
    auto event_activation = metric_activation->add_event_activation();
    event_activation->set_atom_matcher_id(batterySaverStartMatcher.id());
    event_activation->set_ttl_seconds(ttlNs / 1000000000);

    StatsdStats::getInstance().reset();

    ConfigKey cfgKey;
    auto processor = CreateStatsLogProcessor(baseTimeNs, configAddedTimeNs, config, cfgKey,
                                             SharedRefBase::make<FakeSubsystemSleepCallback>(),
                                             util::SUBSYSTEM_SLEEP_STATE);
    ASSERT_EQ(processor->mMetricsManagers.size(), 1u);
    EXPECT_TRUE(processor->mMetricsManagers.begin()->second->isConfigValid());
    processor->mPullerManager->ForceClearPullerCache();

    const int startBucketNum = processor->mMetricsManagers.begin()
                                       ->second->mAllMetricProducers[0]
                                       ->getCurrentBucketNum();
    EXPECT_EQ(startBucketNum, 2);
    EXPECT_FALSE(processor->mMetricsManagers.begin()->second->mAllMetricProducers[0]->isActive());

    // When creating the config, the value metric producer should register the alarm at the
    // end of the current bucket.
    ASSERT_EQ((size_t)1, processor->mPullerManager->mReceivers.size());
    EXPECT_EQ(bucketSizeNs,
              processor->mPullerManager->mReceivers.begin()->second.front().intervalNs);
    int64_t& expectedPullTimeNs =
            processor->mPullerManager->mReceivers.begin()->second.front().nextPullTimeNs;
    EXPECT_EQ(baseTimeNs + startBucketNum * bucketSizeNs + bucketSizeNs, expectedPullTimeNs);

    // Initialize metric.
    const int64_t metricInitTimeNs = configAddedTimeNs + 1;  // 10 mins + 1 ns.
    processor->onStatsdInitCompleted(metricInitTimeNs);

    // Check no pull occurred since metric not active.
    StatsdStatsReport_PulledAtomStats pulledAtomStats =
            getPulledAtomStats(util::SUBSYSTEM_SLEEP_STATE);
    EXPECT_EQ(pulledAtomStats.atom_id(), util::SUBSYSTEM_SLEEP_STATE);
    EXPECT_EQ(pulledAtomStats.total_pull(), 0);

    // Check skip bucket is not added when metric is not active.
    int64_t dumpReportTimeNs = metricInitTimeNs + 1;  // 10 mins + 2 ns.
    vector<uint8_t> buffer;
    processor->onDumpReport(cfgKey, dumpReportTimeNs, true /* include_current_partial_bucket */,
                            true /* erase_data */, ADB_DUMP, FAST, &buffer);
    ConfigMetricsReportList reports;
    EXPECT_TRUE(buffer.size() > 0);
    EXPECT_TRUE(reports.ParseFromArray(&buffer[0], buffer.size()));
    ASSERT_EQ(1, reports.reports_size());
    ASSERT_EQ(1, reports.reports(0).metrics_size());
    StatsLogReport::ValueMetricDataWrapper valueMetrics =
            reports.reports(0).metrics(0).value_metrics();
    EXPECT_EQ(valueMetrics.skipped_size(), 0);

    // App upgrade.
    const int64_t appUpgradeTimeNs = dumpReportTimeNs + 1;  // 10 mins + 3 ns.
    processor->notifyAppUpgrade(appUpgradeTimeNs, "appName", 1000 /* uid */, 2 /* version */);

    // Check no pull occurred since metric not active.
    pulledAtomStats = getPulledAtomStats(util::SUBSYSTEM_SLEEP_STATE);
    EXPECT_EQ(pulledAtomStats.atom_id(), util::SUBSYSTEM_SLEEP_STATE);
    EXPECT_EQ(pulledAtomStats.total_pull(), 0);

    // Check skip bucket is not added when metric is not active.
    dumpReportTimeNs = appUpgradeTimeNs + 1;  // 10 mins + 4 ns.
    buffer.clear();
    processor->onDumpReport(cfgKey, dumpReportTimeNs, true /* include_current_partial_bucket */,
                            true /* erase_data */, ADB_DUMP, FAST, &buffer);
    EXPECT_TRUE(buffer.size() > 0);
    EXPECT_TRUE(reports.ParseFromArray(&buffer[0], buffer.size()));
    ASSERT_EQ(1, reports.reports_size());
    ASSERT_EQ(1, reports.reports(0).metrics_size());
    valueMetrics = reports.reports(0).metrics(0).value_metrics();
    EXPECT_EQ(valueMetrics.skipped_size(), 0);

    // Dump report with a pull. The pull should not happen because metric is inactive.
    dumpReportTimeNs = dumpReportTimeNs + 1;  // 10 mins + 6 ns.
    buffer.clear();
    processor->onDumpReport(cfgKey, dumpReportTimeNs, true /* include_current_partial_bucket */,
                            true /* erase_data */, ADB_DUMP, NO_TIME_CONSTRAINTS, &buffer);
    pulledAtomStats = getPulledAtomStats(util::SUBSYSTEM_SLEEP_STATE);
    EXPECT_EQ(pulledAtomStats.atom_id(), util::SUBSYSTEM_SLEEP_STATE);
    EXPECT_EQ(pulledAtomStats.total_pull(), 0);

    // Check skipped bucket is not added from the dump operation when metric is not active.
    EXPECT_TRUE(buffer.size() > 0);
    EXPECT_TRUE(reports.ParseFromArray(&buffer[0], buffer.size()));
    ASSERT_EQ(1, reports.reports_size());
    ASSERT_EQ(1, reports.reports(0).metrics_size());
    valueMetrics = reports.reports(0).metrics(0).value_metrics();
    EXPECT_EQ(valueMetrics.skipped_size(), 0);

    // Pulling alarm arrives on time and reset the sequential pulling alarm. This bucket is skipped.
    processor->informPullAlarmFired(expectedPullTimeNs + 1);  // 15 mins + 1 ns.
    EXPECT_EQ(baseTimeNs + startBucketNum * bucketSizeNs + 2 * bucketSizeNs, expectedPullTimeNs);
    EXPECT_FALSE(processor->mMetricsManagers.begin()->second->mAllMetricProducers[0]->isActive());

    // Activate the metric. A pull occurs here that sets the base.
    // 15 mins + 2 ms
    const int64_t activationNs = configAddedTimeNs + bucketSizeNs + (2 * 1000 * 1000);  // 2 millis.
    auto batterySaverOnEvent = CreateBatterySaverOnEvent(activationNs);
    processor->OnLogEvent(batterySaverOnEvent.get());  // 15 mins + 2 ms.
    EXPECT_TRUE(processor->mMetricsManagers.begin()->second->mAllMetricProducers[0]->isActive());

    // This bucket should be kept. 1 total
    processor->informPullAlarmFired(expectedPullTimeNs + 1);  // 20 mins + 1 ns.
    EXPECT_EQ(baseTimeNs + startBucketNum * bucketSizeNs + 3 * bucketSizeNs, expectedPullTimeNs);

    // 25 mins + 2 ns.
    // This bucket should be kept. 2 total
    processor->informPullAlarmFired(expectedPullTimeNs + 2);  // 25 mins + 2 ns.
    EXPECT_EQ(baseTimeNs + startBucketNum * bucketSizeNs + 4 * bucketSizeNs, expectedPullTimeNs);

    // Create random event to deactivate metric.
    // A pull occurs here and a partial bucket is created. The bucket ending here is kept. 3 total.
    // 25 mins + 2 ms + 1 ns.
    const int64_t deactivationNs = activationNs + ttlNs + 1;
    auto deactivationEvent = CreateScreenBrightnessChangedEvent(deactivationNs, 50);
    processor->OnLogEvent(deactivationEvent.get());
    EXPECT_FALSE(processor->mMetricsManagers.begin()->second->mAllMetricProducers[0]->isActive());

    // 30 mins + 3 ns. This bucket is skipped.
    processor->informPullAlarmFired(expectedPullTimeNs + 3);
    EXPECT_EQ(baseTimeNs + startBucketNum * bucketSizeNs + 5 * bucketSizeNs, expectedPullTimeNs);

    // 35 mins + 4 ns. This bucket is skipped
    processor->informPullAlarmFired(expectedPullTimeNs + 4);
    EXPECT_EQ(baseTimeNs + startBucketNum * bucketSizeNs + 6 * bucketSizeNs, expectedPullTimeNs);

    dumpReportTimeNs = configAddedTimeNs + 6 * bucketSizeNs + 10;
    buffer.clear();
    // 40 mins + 10 ns.
    processor->onDumpReport(cfgKey, dumpReportTimeNs, false /* include_current_partial_bucket */,
                            true /* erase_data */, ADB_DUMP, FAST, &buffer);
    EXPECT_TRUE(buffer.size() > 0);
    EXPECT_TRUE(reports.ParseFromArray(&buffer[0], buffer.size()));
    backfillDimensionPath(&reports);
    backfillStringInReport(&reports);
    backfillStartEndTimestamp(&reports);
    ASSERT_EQ(1, reports.reports_size());
    ASSERT_EQ(1, reports.reports(0).metrics_size());
    valueMetrics = StatsLogReport::ValueMetricDataWrapper();
    sortMetricDataByDimensionsValue(reports.reports(0).metrics(0).value_metrics(), &valueMetrics);
    ASSERT_GT((int)valueMetrics.data_size(), 0);

    auto data = valueMetrics.data(0);
    EXPECT_EQ(util::SUBSYSTEM_SLEEP_STATE, data.dimensions_in_what().field());
    ASSERT_EQ(1, data.dimensions_in_what().value_tuple().dimensions_value_size());
    EXPECT_EQ(1 /* subsystem name field */,
              data.dimensions_in_what().value_tuple().dimensions_value(0).field());
    EXPECT_FALSE(data.dimensions_in_what().value_tuple().dimensions_value(0).value_str().empty());
    // We have 3 full buckets, the two surrounding the activation are dropped.
    ASSERT_EQ(3, data.bucket_info_size());

    auto bucketInfo = data.bucket_info(0);
    EXPECT_EQ(baseTimeNs + 3 * bucketSizeNs, bucketInfo.start_bucket_elapsed_nanos());
    EXPECT_EQ(baseTimeNs + 4 * bucketSizeNs, bucketInfo.end_bucket_elapsed_nanos());
    ASSERT_EQ(1, bucketInfo.values_size());

    bucketInfo = data.bucket_info(1);
    EXPECT_EQ(baseTimeNs + 4 * bucketSizeNs, bucketInfo.start_bucket_elapsed_nanos());
    EXPECT_EQ(baseTimeNs + 5 * bucketSizeNs, bucketInfo.end_bucket_elapsed_nanos());
    ASSERT_EQ(1, bucketInfo.values_size());

    bucketInfo = data.bucket_info(2);
    EXPECT_EQ(MillisToNano(NanoToMillis(baseTimeNs + 5 * bucketSizeNs)),
              bucketInfo.start_bucket_elapsed_nanos());
    EXPECT_EQ(MillisToNano(NanoToMillis(deactivationNs)), bucketInfo.end_bucket_elapsed_nanos());
    ASSERT_EQ(1, bucketInfo.values_size());

    // Check skipped bucket is not added after deactivation.
    dumpReportTimeNs = configAddedTimeNs + 7 * bucketSizeNs + 10;
    buffer.clear();
    // 45 mins + 10 ns.
    processor->onDumpReport(cfgKey, dumpReportTimeNs, true /* include_current_partial_bucket */,
                            true /* erase_data */, ADB_DUMP, FAST, &buffer);
    EXPECT_TRUE(buffer.size() > 0);
    EXPECT_TRUE(reports.ParseFromArray(&buffer[0], buffer.size()));
    ASSERT_EQ(1, reports.reports_size());
    ASSERT_EQ(1, reports.reports(0).metrics_size());
    valueMetrics = reports.reports(0).metrics(0).value_metrics();
    EXPECT_EQ(valueMetrics.skipped_size(), 0);
}

/**
 * Test initialization of a simple value metric that is sliced by a state.
 *
 * ValueCpuUserTimePerScreenState
 */
TEST(ValueMetricE2eTest, TestInitWithSlicedState) {
    // Create config.
    StatsdConfig config;

    auto pulledAtomMatcher =
            CreateSimpleAtomMatcher("TestMatcher", util::SUBSYSTEM_SLEEP_STATE);
    *config.add_atom_matcher() = pulledAtomMatcher;

    auto screenState = CreateScreenState();
    *config.add_state() = screenState;

    // Create value metric that slices by screen state without a map.
    int64_t metricId = 123456;
    auto valueMetric = config.add_value_metric();
    valueMetric->set_id(metricId);
    valueMetric->set_bucket(TimeUnit::FIVE_MINUTES);
    valueMetric->set_what(pulledAtomMatcher.id());
    *valueMetric->mutable_value_field() =
            CreateDimensions(util::CPU_TIME_PER_UID, {2 /* user_time_micros */});
    valueMetric->add_slice_by_state(screenState.id());
    valueMetric->set_max_pull_delay_sec(INT_MAX);

    // Initialize StatsLogProcessor.
    const uint64_t bucketStartTimeNs = 10000000000;  // 0:10
    const uint64_t bucketSizeNs =
            TimeUnitToBucketSizeInMillis(config.value_metric(0).bucket()) * 1000000LL;
    int uid = 12345;
    int64_t cfgId = 98765;
    ConfigKey cfgKey(uid, cfgId);

    auto processor = CreateStatsLogProcessor(bucketStartTimeNs, bucketStartTimeNs, config, cfgKey);

    // Check that StateTrackers were initialized correctly.
    EXPECT_EQ(1, StateManager::getInstance().getStateTrackersCount());
    EXPECT_EQ(1, StateManager::getInstance().getListenersCount(SCREEN_STATE_ATOM_ID));

    // Check that NumericValueMetricProducer was initialized correctly.
    ASSERT_EQ(1U, processor->mMetricsManagers.size());
    sp<MetricsManager> metricsManager = processor->mMetricsManagers.begin()->second;
    EXPECT_TRUE(metricsManager->isConfigValid());
    ASSERT_EQ(1, metricsManager->mAllMetricProducers.size());
    sp<MetricProducer> metricProducer = metricsManager->mAllMetricProducers[0];
    ASSERT_EQ(1, metricProducer->mSlicedStateAtoms.size());
    EXPECT_EQ(SCREEN_STATE_ATOM_ID, metricProducer->mSlicedStateAtoms.at(0));
    ASSERT_EQ(0, metricProducer->mStateGroupMap.size());
}

/**
 * Test initialization of a value metric that is sliced by state and has
 * dimensions_in_what.
 *
 * ValueCpuUserTimePerUidPerUidProcessState
 */
TEST(ValueMetricE2eTest, TestInitWithSlicedState_WithDimensions) {
    // Create config.
    StatsdConfig config;

    auto cpuTimePerUidMatcher =
            CreateSimpleAtomMatcher("CpuTimePerUidMatcher", util::CPU_TIME_PER_UID);
    *config.add_atom_matcher() = cpuTimePerUidMatcher;

    auto uidProcessState = CreateUidProcessState();
    *config.add_state() = uidProcessState;

    // Create value metric that slices by screen state with a complete map.
    int64_t metricId = 123456;
    auto valueMetric = config.add_value_metric();
    valueMetric->set_id(metricId);
    valueMetric->set_bucket(TimeUnit::FIVE_MINUTES);
    valueMetric->set_what(cpuTimePerUidMatcher.id());
    *valueMetric->mutable_value_field() =
            CreateDimensions(util::CPU_TIME_PER_UID, {2 /* user_time_micros */});
    *valueMetric->mutable_dimensions_in_what() =
            CreateDimensions(util::CPU_TIME_PER_UID, {1 /* uid */});
    valueMetric->add_slice_by_state(uidProcessState.id());
    MetricStateLink* stateLink = valueMetric->add_state_link();
    stateLink->set_state_atom_id(UID_PROCESS_STATE_ATOM_ID);
    auto fieldsInWhat = stateLink->mutable_fields_in_what();
    *fieldsInWhat = CreateDimensions(util::CPU_TIME_PER_UID, {1 /* uid */});
    auto fieldsInState = stateLink->mutable_fields_in_state();
    *fieldsInState = CreateDimensions(UID_PROCESS_STATE_ATOM_ID, {1 /* uid */});
    valueMetric->set_max_pull_delay_sec(INT_MAX);

    // Initialize StatsLogProcessor.
    const uint64_t bucketStartTimeNs = 10000000000;  // 0:10
    int uid = 12345;
    int64_t cfgId = 98765;
    ConfigKey cfgKey(uid, cfgId);

    auto processor = CreateStatsLogProcessor(bucketStartTimeNs, bucketStartTimeNs, config, cfgKey);

    // Check that StateTrackers were initialized correctly.
    EXPECT_EQ(1, StateManager::getInstance().getStateTrackersCount());
    EXPECT_EQ(1, StateManager::getInstance().getListenersCount(UID_PROCESS_STATE_ATOM_ID));

    // Check that NumericValueMetricProducer was initialized correctly.
    ASSERT_EQ(1U, processor->mMetricsManagers.size());
    sp<MetricsManager> metricsManager = processor->mMetricsManagers.begin()->second;
    EXPECT_TRUE(metricsManager->isConfigValid());
    ASSERT_EQ(1, metricsManager->mAllMetricProducers.size());
    sp<MetricProducer> metricProducer = metricsManager->mAllMetricProducers[0];
    ASSERT_EQ(1, metricProducer->mSlicedStateAtoms.size());
    EXPECT_EQ(UID_PROCESS_STATE_ATOM_ID, metricProducer->mSlicedStateAtoms.at(0));
    ASSERT_EQ(0, metricProducer->mStateGroupMap.size());
}

/**
 * Test initialization of a value metric that is sliced by state and has
 * dimensions_in_what.
 *
 * ValueCpuUserTimePerUidPerUidProcessState
 */
TEST(ValueMetricE2eTest, TestInitWithSlicedState_WithIncorrectDimensions) {
    // Create config.
    StatsdConfig config;

    auto cpuTimePerUidMatcher =
            CreateSimpleAtomMatcher("CpuTimePerUidMatcher", util::CPU_TIME_PER_UID);
    *config.add_atom_matcher() = cpuTimePerUidMatcher;

    auto uidProcessState = CreateUidProcessState();
    *config.add_state() = uidProcessState;

    // Create value metric that slices by screen state with a complete map.
    int64_t metricId = 123456;
    auto valueMetric = config.add_value_metric();
    valueMetric->set_id(metricId);
    valueMetric->set_bucket(TimeUnit::FIVE_MINUTES);
    valueMetric->set_what(cpuTimePerUidMatcher.id());
    *valueMetric->mutable_value_field() =
            CreateDimensions(util::CPU_TIME_PER_UID, {2 /* user_time_micros */});
    valueMetric->add_slice_by_state(uidProcessState.id());
    MetricStateLink* stateLink = valueMetric->add_state_link();
    stateLink->set_state_atom_id(UID_PROCESS_STATE_ATOM_ID);
    auto fieldsInWhat = stateLink->mutable_fields_in_what();
    *fieldsInWhat = CreateDimensions(util::CPU_TIME_PER_UID, {1 /* uid */});
    auto fieldsInState = stateLink->mutable_fields_in_state();
    *fieldsInState = CreateDimensions(UID_PROCESS_STATE_ATOM_ID, {1 /* uid */});
    valueMetric->set_max_pull_delay_sec(INT_MAX);

    // Initialize StatsLogProcessor.
    const uint64_t bucketStartTimeNs = 10000000000;  // 0:10
    int uid = 12345;
    int64_t cfgId = 98765;
    ConfigKey cfgKey(uid, cfgId);
    auto processor = CreateStatsLogProcessor(bucketStartTimeNs, bucketStartTimeNs, config, cfgKey);

    // No StateTrackers are initialized.
    EXPECT_EQ(0, StateManager::getInstance().getStateTrackersCount());

    // Config initialization fails.
    ASSERT_EQ(0, processor->mMetricsManagers.size());
}

TEST(ValueMetricE2eTest, TestInitWithValueFieldPositionALL) {
    // Create config.
    StatsdConfig config;

    AtomMatcher testAtomReportedMatcher =
            CreateSimpleAtomMatcher("TestAtomReportedMatcher", util::TEST_ATOM_REPORTED);
    *config.add_atom_matcher() = testAtomReportedMatcher;

    // Create value metric.
    int64_t metricId = 123456;
    ValueMetric* valueMetric = config.add_value_metric();
    valueMetric->set_id(metricId);
    valueMetric->set_bucket(TimeUnit::FIVE_MINUTES);
    valueMetric->set_what(testAtomReportedMatcher.id());
    *valueMetric->mutable_value_field() = CreateRepeatedDimensions(
            util::TEST_ATOM_REPORTED, {9 /*repeated_int_field*/}, {Position::ALL});

    // Initialize StatsLogProcessor.
    const uint64_t bucketStartTimeNs = 10000000000;  // 0:10
    int uid = 12345;
    int64_t cfgId = 98765;
    ConfigKey cfgKey(uid, cfgId);
    sp<StatsLogProcessor> processor =
            CreateStatsLogProcessor(bucketStartTimeNs, bucketStartTimeNs, config, cfgKey);

    // Config initialization fails.
    ASSERT_EQ(0, processor->mMetricsManagers.size());
}

TEST(ValueMetricE2eTest, TestInitWithMultipleAggTypes) {
    // Create config.
    StatsdConfig config;

    AtomMatcher testAtomReportedMatcher =
            CreateSimpleAtomMatcher("TestAtomReportedMatcher", util::TEST_ATOM_REPORTED);
    *config.add_atom_matcher() = testAtomReportedMatcher;

    // Create value metric.
    int64_t metricId = 123456;
    ValueMetric* valueMetric = config.add_value_metric();
    valueMetric->set_id(metricId);
    valueMetric->set_bucket(TimeUnit::FIVE_MINUTES);
    valueMetric->set_what(testAtomReportedMatcher.id());
    *valueMetric->mutable_value_field() = CreateDimensions(
            util::TEST_ATOM_REPORTED, {2 /*int_field*/, 2 /*int_field*/, 3 /*long_field*/,
                                       3 /*long_field*/, 3 /*long_field*/});
    valueMetric->add_aggregation_types(ValueMetric::SUM);
    valueMetric->add_aggregation_types(ValueMetric::MIN);
    valueMetric->add_aggregation_types(ValueMetric::MAX);
    valueMetric->add_aggregation_types(ValueMetric::AVG);
    valueMetric->add_aggregation_types(ValueMetric::MIN);

    // Initialize StatsLogProcessor.
    const uint64_t bucketStartTimeNs = 10000000000;  // 0:10
    int uid = 12345;
    int64_t cfgId = 98765;
    ConfigKey cfgKey(uid, cfgId);
    sp<StatsLogProcessor> processor =
            CreateStatsLogProcessor(bucketStartTimeNs, bucketStartTimeNs, config, cfgKey);

    ASSERT_EQ(1, processor->mMetricsManagers.size());
    sp<MetricsManager> metricsManager = processor->mMetricsManagers.begin()->second;
    EXPECT_TRUE(metricsManager->isConfigValid());
    ASSERT_EQ(1, metricsManager->mAllMetricProducers.size());
    sp<MetricProducer> metricProducer = metricsManager->mAllMetricProducers[0];
    NumericValueMetricProducer* valueProducer =
            static_cast<NumericValueMetricProducer*>(metricProducer.get());
    ASSERT_EQ(5u, valueProducer->mAggregationTypes.size());
    EXPECT_EQ(ValueMetric::SUM, valueProducer->mAggregationTypes[0]);
    EXPECT_EQ(ValueMetric::MIN, valueProducer->mAggregationTypes[1]);
    EXPECT_EQ(ValueMetric::MAX, valueProducer->mAggregationTypes[2]);
    EXPECT_EQ(ValueMetric::AVG, valueProducer->mAggregationTypes[3]);
    EXPECT_EQ(ValueMetric::MIN, valueProducer->mAggregationTypes[4]);
    EXPECT_TRUE(valueProducer->mIncludeSampleSize);
}

TEST(ValueMetricE2eTest, TestInitWithDefaultAggType) {
    // Create config.
    StatsdConfig config;

    AtomMatcher testAtomReportedMatcher =
            CreateSimpleAtomMatcher("TestAtomReportedMatcher", util::TEST_ATOM_REPORTED);
    *config.add_atom_matcher() = testAtomReportedMatcher;

    // Create value metric.
    int64_t metricId = 123456;
    ValueMetric* valueMetric = config.add_value_metric();
    valueMetric->set_id(metricId);
    valueMetric->set_bucket(TimeUnit::FIVE_MINUTES);
    valueMetric->set_what(testAtomReportedMatcher.id());
    *valueMetric->mutable_value_field() =
            CreateDimensions(util::TEST_ATOM_REPORTED, {3 /*long_field*/, 2 /*int_field*/});

    // Initialize StatsLogProcessor.
    const uint64_t bucketStartTimeNs = 10000000000;  // 0:10
    int uid = 12345;
    int64_t cfgId = 98765;
    ConfigKey cfgKey(uid, cfgId);
    sp<StatsLogProcessor> processor =
            CreateStatsLogProcessor(bucketStartTimeNs, bucketStartTimeNs, config, cfgKey);

    ASSERT_EQ(1, processor->mMetricsManagers.size());
    sp<MetricsManager> metricsManager = processor->mMetricsManagers.begin()->second;
    EXPECT_TRUE(metricsManager->isConfigValid());
    ASSERT_EQ(1, metricsManager->mAllMetricProducers.size());
    sp<MetricProducer> metricProducer = metricsManager->mAllMetricProducers[0];
    NumericValueMetricProducer* valueProducer =
            static_cast<NumericValueMetricProducer*>(metricProducer.get());
    ASSERT_EQ(1u, valueProducer->mAggregationTypes.size());
    EXPECT_EQ(ValueMetric::SUM, valueProducer->mAggregationTypes[0]);
    EXPECT_FALSE(valueProducer->mIncludeSampleSize);
}

#else
GTEST_LOG_(INFO) << "This test does nothing.\n";
#endif

}  // namespace statsd
}  // namespace os
}  // namespace android
