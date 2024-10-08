/*
 * Copyright (C) 2018 The Android Open Source Project
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
package android.cts.statsd.metric;

import static com.google.common.truth.Truth.assertThat;

import android.cts.statsdatom.lib.AtomTestUtils;
import android.cts.statsdatom.lib.ConfigUtils;
import android.cts.statsdatom.lib.ReportUtils;

import com.android.internal.os.StatsdConfigProto.ActivationType;
import com.android.internal.os.StatsdConfigProto.AtomMatcher;
import com.android.internal.os.StatsdConfigProto.EventActivation;
import com.android.internal.os.StatsdConfigProto.FieldMatcher;
import com.android.internal.os.StatsdConfigProto.MetricActivation;
import com.android.internal.os.StatsdConfigProto.Predicate;
import com.android.internal.os.StatsdConfigProto.SimpleAtomMatcher;
import com.android.internal.os.StatsdConfigProto.SimplePredicate;
import com.android.internal.os.StatsdConfigProto.StatsdConfig;
import com.android.internal.os.StatsdConfigProto.TimeUnit;
import com.android.internal.os.StatsdConfigProto.ValueMetric;

import com.android.os.AtomsProto.AppBreadcrumbReported;
import com.android.os.AtomsProto.Atom;
import com.android.os.AtomsProto.SystemElapsedRealtime;
import com.android.os.StatsLog.StatsLogReport;
import com.android.os.StatsLog.ValueBucketInfo;
import com.android.os.StatsLog.ValueMetricData;

import com.android.tradefed.log.LogUtil;
import com.android.tradefed.testtype.DeviceTestCase;
import com.android.tradefed.util.RunUtil;

import com.google.protobuf.ExtensionRegistry;

public class ValueMetricsTests extends DeviceTestCase {
    private static final int APP_BREADCRUMB_REPORTED_A_MATCH_START_ID = 0;
    private static final int APP_BREADCRUMB_REPORTED_A_MATCH_STOP_ID = 1;
    private static final int APP_BREADCRUMB_REPORTED_B_MATCH_START_ID = 2;

    @Override
    protected void setUp() throws Exception {
        super.setUp();
        ConfigUtils.removeConfig(getDevice());
        ReportUtils.clearReports(getDevice());
        RunUtil.getDefault().sleep(1000);
    }

    @Override
    protected void tearDown() throws Exception {
        ConfigUtils.removeConfig(getDevice());
        ReportUtils.clearReports(getDevice());
        super.tearDown();
    }


    public void testValueMetric() throws Exception {
        // Add AtomMatcher's.
        AtomMatcher startAtomMatcher =
                MetricsUtils.startAtomMatcher(APP_BREADCRUMB_REPORTED_A_MATCH_START_ID);
        AtomMatcher stopAtomMatcher =
                MetricsUtils.stopAtomMatcher(APP_BREADCRUMB_REPORTED_A_MATCH_STOP_ID);
        AtomMatcher atomMatcher =
                MetricsUtils.simpleAtomMatcher(APP_BREADCRUMB_REPORTED_B_MATCH_START_ID);

        StatsdConfig.Builder builder = ConfigUtils.createConfigBuilder(
                MetricsUtils.DEVICE_SIDE_TEST_PACKAGE);
        builder.addAtomMatcher(startAtomMatcher);
        builder.addAtomMatcher(stopAtomMatcher);
        builder.addAtomMatcher(atomMatcher);

        // Add ValueMetric.
        builder.addValueMetric(ValueMetric.newBuilder()
                .setId(MetricsUtils.VALUE_METRIC_ID)
                .setWhat(APP_BREADCRUMB_REPORTED_B_MATCH_START_ID)
                .setBucket(TimeUnit.CTS)
                .setValueField(FieldMatcher.newBuilder()
                        .setField(Atom.APP_BREADCRUMB_REPORTED_FIELD_NUMBER)
                        .addChild(FieldMatcher.newBuilder().setField(
                                AppBreadcrumbReported.LABEL_FIELD_NUMBER)))
                .setDimensionsInWhat(FieldMatcher.newBuilder()
                        .setField(APP_BREADCRUMB_REPORTED_B_MATCH_START_ID)
                        .build())
                .build());

        // Upload config.
        ConfigUtils.uploadConfig(getDevice(), builder);

        // Create AppBreadcrumbReported Start/Stop events.
        AtomTestUtils.sendAppBreadcrumbReportedAtom(getDevice(),
                AppBreadcrumbReported.State.START.getNumber(), 1);
        RunUtil.getDefault().sleep(1000);
        AtomTestUtils.sendAppBreadcrumbReportedAtom(getDevice(),
                AppBreadcrumbReported.State.STOP.getNumber(), 1);
        AtomTestUtils.sendAppBreadcrumbReportedAtom(getDevice(),
                AppBreadcrumbReported.State.START.getNumber(), 3);
        AtomTestUtils.sendAppBreadcrumbReportedAtom(getDevice(),
                AppBreadcrumbReported.State.STOP.getNumber(), 3);

        // Wait for the metrics to propagate to statsd.
        RunUtil.getDefault().sleep(1000);

        StatsLogReport metricReport = ReportUtils.getStatsLogReport(getDevice(),
                ExtensionRegistry.getEmptyRegistry());
        LogUtil.CLog.d("Got the following value metric data: " + metricReport.toString());
        assertThat(metricReport.getMetricId()).isEqualTo(MetricsUtils.VALUE_METRIC_ID);
        assertThat(metricReport.hasValueMetrics()).isTrue();
        StatsLogReport.ValueMetricDataWrapper valueData = metricReport.getValueMetrics();
        assertThat(valueData.getDataCount()).isEqualTo(1);

        int bucketCount = valueData.getData(0).getBucketInfoCount();
        assertThat(bucketCount).isGreaterThan(1);
        ValueMetricData data = valueData.getData(0);
        int totalValue = 0;
        for (ValueBucketInfo bucketInfo : data.getBucketInfoList()) {
            MetricsUtils.assertBucketTimePresent(bucketInfo);
            assertThat(bucketInfo.getValuesCount()).isEqualTo(1);
            assertThat(bucketInfo.getValues(0).getIndex()).isEqualTo(0);
            totalValue += (int) bucketInfo.getValues(0).getValueLong();
        }
        assertThat(totalValue).isEqualTo(8);
    }

    // Test value metric with pulled atoms and across multiple buckets
    public void testPullerAcrossBuckets() throws Exception {
        // Add AtomMatcher's.
        final String predicateTrueName = "APP_BREADCRUMB_REPORTED_START";
        final String predicateFalseName = "APP_BREADCRUMB_REPORTED_STOP";
        final String predicateName = "APP_BREADCRUMB_REPORTED_IS_STOP";

        AtomMatcher startAtomMatcher =
                MetricsUtils.startAtomMatcher(predicateTrueName.hashCode());
        AtomMatcher stopAtomMatcher =
                MetricsUtils.stopAtomMatcher(predicateFalseName.hashCode());

        StatsdConfig.Builder builder = ConfigUtils.createConfigBuilder(
                MetricsUtils.DEVICE_SIDE_TEST_PACKAGE);
        builder.addAtomMatcher(startAtomMatcher);
        builder.addAtomMatcher(stopAtomMatcher);
        builder.addPredicate(Predicate.newBuilder()
                .setId(predicateName.hashCode())
                .setSimplePredicate(SimplePredicate.newBuilder()
                        .setStart(predicateTrueName.hashCode())
                        .setStop(predicateFalseName.hashCode())
                        .setCountNesting(false)
                )
        );

        final String atomName = "SYSTEM_ELAPSED_REALTIME";
        SimpleAtomMatcher.Builder sam = SimpleAtomMatcher.newBuilder().setAtomId(
                Atom.SYSTEM_ELAPSED_REALTIME_FIELD_NUMBER);
        builder.addAtomMatcher(AtomMatcher.newBuilder()
                .setId(atomName.hashCode())
                .setSimpleAtomMatcher(sam));

        // Add ValueMetric.
        builder.addValueMetric(ValueMetric.newBuilder()
                .setId(MetricsUtils.VALUE_METRIC_ID)
                .setWhat(atomName.hashCode())
                .setBucket(TimeUnit.ONE_MINUTE)
                .setValueField(FieldMatcher.newBuilder()
                        .setField(Atom.SYSTEM_ELAPSED_REALTIME_FIELD_NUMBER)
                        .addChild(FieldMatcher.newBuilder().setField(
                                SystemElapsedRealtime.TIME_MILLIS_FIELD_NUMBER)))
                .setCondition(predicateName.hashCode())
                .build());

        // Upload config.
        ConfigUtils.uploadConfig(getDevice(), builder);

        // Create AppBreadcrumbReported Start/Stop events.
        AtomTestUtils.sendAppBreadcrumbReportedAtom(getDevice(),
                AppBreadcrumbReported.State.START.getNumber(), 1);
        // Wait for 2 min and 1 sec to capture at least 2 buckets
        RunUtil.getDefault().sleep(2 * 60_000 + 10_000);
        AtomTestUtils.sendAppBreadcrumbReportedAtom(getDevice(),
                AppBreadcrumbReported.State.STOP.getNumber(), 1);

        // Wait for the metrics to propagate to statsd.
        RunUtil.getDefault().sleep(1_000);

        StatsLogReport metricReport = ReportUtils.getStatsLogReport(getDevice(),
                ExtensionRegistry.getEmptyRegistry());
        LogUtil.CLog.d("Got the following value metric data: " + metricReport.toString());
        assertThat(metricReport.getMetricId()).isEqualTo(MetricsUtils.VALUE_METRIC_ID);
        assertThat(metricReport.hasValueMetrics()).isTrue();
        StatsLogReport.ValueMetricDataWrapper valueData = metricReport.getValueMetrics();
        assertThat(valueData.getDataCount()).isEqualTo(1);

        int bucketCount = valueData.getData(0).getBucketInfoCount();
        // should have at least 2 buckets
        assertThat(bucketCount).isAtLeast(2);
        ValueMetricData data = valueData.getData(0);
        int totalValue = 0;
        for (ValueBucketInfo bucketInfo : data.getBucketInfoList()) {
            MetricsUtils.assertBucketTimePresent(bucketInfo);
            assertThat(bucketInfo.getValuesCount()).isEqualTo(1);
            assertThat(bucketInfo.getValues(0).getIndex()).isEqualTo(0);
            totalValue += (int) bucketInfo.getValues(0).getValueLong();
        }
        // At most we lose one full min bucket
        assertThat(totalValue).isGreaterThan(130_000 - 60_000);
    }

    // Test value metric with pulled atoms and across multiple buckets
    public void testMultipleEventsPerBucket() throws Exception {
        // Add AtomMatcher's.
        final String predicateTrueName = "APP_BREADCRUMB_REPORTED_START";
        final String predicateFalseName = "APP_BREADCRUMB_REPORTED_STOP";
        final String predicateName = "APP_BREADCRUMB_REPORTED_IS_STOP";

        AtomMatcher startAtomMatcher =
                MetricsUtils.startAtomMatcher(predicateTrueName.hashCode());
        AtomMatcher stopAtomMatcher =
                MetricsUtils.stopAtomMatcher(predicateFalseName.hashCode());

        StatsdConfig.Builder builder = ConfigUtils.createConfigBuilder(
                MetricsUtils.DEVICE_SIDE_TEST_PACKAGE);
        builder.addAtomMatcher(startAtomMatcher);
        builder.addAtomMatcher(stopAtomMatcher);
        builder.addPredicate(Predicate.newBuilder()
                .setId(predicateName.hashCode())
                .setSimplePredicate(SimplePredicate.newBuilder()
                        .setStart(predicateTrueName.hashCode())
                        .setStop(predicateFalseName.hashCode())
                        .setCountNesting(false)
                )
        );

        final String atomName = "SYSTEM_ELAPSED_REALTIME";
        SimpleAtomMatcher.Builder sam = SimpleAtomMatcher.newBuilder().setAtomId(
                Atom.SYSTEM_ELAPSED_REALTIME_FIELD_NUMBER);
        builder.addAtomMatcher(AtomMatcher.newBuilder()
                .setId(atomName.hashCode())
                .setSimpleAtomMatcher(sam));

        // Add ValueMetric.
        builder.addValueMetric(ValueMetric.newBuilder()
                .setId(MetricsUtils.VALUE_METRIC_ID)
                .setWhat(atomName.hashCode())
                .setBucket(TimeUnit.ONE_MINUTE)
                .setValueField(FieldMatcher.newBuilder()
                        .setField(Atom.SYSTEM_ELAPSED_REALTIME_FIELD_NUMBER)
                        .addChild(FieldMatcher.newBuilder().setField(
                                SystemElapsedRealtime.TIME_MILLIS_FIELD_NUMBER)))
                .setCondition(predicateName.hashCode())
                .build());

        // Upload config.
        ConfigUtils.uploadConfig(getDevice(), builder);

        final int NUM_EVENTS = 10;
        final long GAP_INTERVAL = 10_000;
        // Create AppBreadcrumbReported Start/Stop events.
        for (int i = 0; i < NUM_EVENTS; i++) {
            AtomTestUtils.sendAppBreadcrumbReportedAtom(getDevice(),
                    AppBreadcrumbReported.State.START.getNumber(), 1);
            RunUtil.getDefault().sleep(GAP_INTERVAL);
            AtomTestUtils.sendAppBreadcrumbReportedAtom(getDevice(),
                    AppBreadcrumbReported.State.STOP.getNumber(), 1);
            RunUtil.getDefault().sleep(GAP_INTERVAL);
        }

        // Wait for the metrics to propagate to statsd.
        RunUtil.getDefault().sleep(1_000);

        StatsLogReport metricReport = ReportUtils.getStatsLogReport(getDevice(),
                ExtensionRegistry.getEmptyRegistry());
        LogUtil.CLog.d("Got the following value metric data: " + metricReport.toString());
        assertThat(metricReport.getMetricId()).isEqualTo(MetricsUtils.VALUE_METRIC_ID);
        assertThat(metricReport.hasValueMetrics()).isTrue();
        StatsLogReport.ValueMetricDataWrapper valueData = metricReport.getValueMetrics();
        assertThat(valueData.getDataCount()).isEqualTo(1);

        int bucketCount = valueData.getData(0).getBucketInfoCount();
        // should have at least 2 buckets
        assertThat(bucketCount).isAtLeast(2);
        ValueMetricData data = valueData.getData(0);
        int totalValue = 0;
        for (ValueBucketInfo bucketInfo : data.getBucketInfoList()) {
            MetricsUtils.assertBucketTimePresent(bucketInfo);
            assertThat(bucketInfo.getValuesCount()).isEqualTo(1);
            assertThat(bucketInfo.getValues(0).getIndex()).isEqualTo(0);
            totalValue += (int) bucketInfo.getValues(0).getValueLong();
        }
        // At most we lose one full min bucket
        assertThat((long) totalValue).isGreaterThan(GAP_INTERVAL * NUM_EVENTS - 60_000);
    }

    // Test value metric with pulled atoms and across multiple buckets
    public void testPullerAcrossBucketsWithActivation() throws Exception {
        StatsdConfig.Builder builder = ConfigUtils.createConfigBuilder(
                MetricsUtils.DEVICE_SIDE_TEST_PACKAGE);

        // Add AtomMatcher's.
        int activationAtomMatcherId = 1;
        int activationAtomMatcherLabel = 1;
        AtomMatcher activationAtomMatcher =
                MetricsUtils.appBreadcrumbMatcherWithLabel(
                        activationAtomMatcherId, activationAtomMatcherLabel);
        final String atomName = "SYSTEM_ELAPSED_REALTIME";
        SimpleAtomMatcher.Builder sam = SimpleAtomMatcher.newBuilder()
                .setAtomId(Atom.SYSTEM_ELAPSED_REALTIME_FIELD_NUMBER);
        builder.addAtomMatcher(activationAtomMatcher)
                .addAtomMatcher(AtomMatcher.newBuilder()
                        .setId(atomName.hashCode())
                        .setSimpleAtomMatcher(sam));

        // Add ValueMetric.
        builder.addValueMetric(ValueMetric.newBuilder()
                .setId(MetricsUtils.VALUE_METRIC_ID)
                .setWhat(atomName.hashCode())
                .setBucket(TimeUnit.ONE_MINUTE)
                .setValueField(FieldMatcher.newBuilder()
                        .setField(Atom.SYSTEM_ELAPSED_REALTIME_FIELD_NUMBER)
                        .addChild(FieldMatcher.newBuilder().setField(
                                SystemElapsedRealtime.TIME_MILLIS_FIELD_NUMBER)))
                .build());
        // Add activation.
        builder.addMetricActivation(MetricActivation.newBuilder()
                .setMetricId(MetricsUtils.VALUE_METRIC_ID)
                .setActivationType(ActivationType.ACTIVATE_IMMEDIATELY)
                .addEventActivation(EventActivation.newBuilder()
                        .setAtomMatcherId(activationAtomMatcherId)
                        .setTtlSeconds(5)));


        // Upload config.
        ConfigUtils.uploadConfig(getDevice(), builder);

        // Wait for 1 min and 10 sec to capture at least 1 bucket
        RunUtil.getDefault().sleep(60_000 + 10_000);

        // Wait for the metrics to propagate to statsd.
        RunUtil.getDefault().sleep(1_000);

        StatsLogReport metricReport = ReportUtils.getStatsLogReport(getDevice(),
                ExtensionRegistry.getEmptyRegistry());
        LogUtil.CLog.d("Got the following value metric data: " + metricReport.toString());
        assertThat(metricReport.getMetricId()).isEqualTo(MetricsUtils.VALUE_METRIC_ID);
        assertThat(metricReport.getValueMetrics().getDataList()).isEmpty();
        // Skipped buckets are not added when metric is empty.
        assertThat(metricReport.getValueMetrics().getSkippedList()).isEmpty();
    }

    public void testValueMetricWithConditionAndActivation() throws Exception {
        final int conditionLabel = 2;
        final int activationMatcherId = 5;
        final int activationMatcherLabel = 5;
        final int whatMatcherId = 8;
        final int ttlSec = 5;

        // Add AtomMatchers.
        AtomMatcher conditionStartAtomMatcher = MetricsUtils.startAtomMatcherWithLabel(
                APP_BREADCRUMB_REPORTED_A_MATCH_START_ID, conditionLabel);
        AtomMatcher conditionStopAtomMatcher = MetricsUtils.stopAtomMatcherWithLabel(
                APP_BREADCRUMB_REPORTED_A_MATCH_STOP_ID, conditionLabel);
        AtomMatcher activationMatcher =
                MetricsUtils.startAtomMatcherWithLabel(
                        activationMatcherId, activationMatcherLabel);
        AtomMatcher whatMatcher =
                MetricsUtils.unspecifiedAtomMatcher(whatMatcherId);

        StatsdConfig.Builder builder = ConfigUtils.createConfigBuilder(
                        MetricsUtils.DEVICE_SIDE_TEST_PACKAGE)
                .addAtomMatcher(conditionStartAtomMatcher)
                .addAtomMatcher(conditionStopAtomMatcher)
                .addAtomMatcher(whatMatcher)
                .addAtomMatcher(activationMatcher);

        // Add Predicates.
        SimplePredicate simplePredicate = SimplePredicate.newBuilder()
                .setStart(APP_BREADCRUMB_REPORTED_A_MATCH_START_ID)
                .setStop(APP_BREADCRUMB_REPORTED_A_MATCH_STOP_ID)
                .build();
        Predicate predicate = Predicate.newBuilder()
                .setId(MetricsUtils.StringToId("Predicate"))
                .setSimplePredicate(simplePredicate)
                .build();
        builder.addPredicate(predicate);

        // Add ValueMetric.
        builder.addValueMetric(ValueMetric.newBuilder()
                        .setId(MetricsUtils.VALUE_METRIC_ID)
                        .setWhat(whatMatcher.getId())
                        .setBucket(TimeUnit.ONE_MINUTE)
                        .setCondition(predicate.getId())
                        .setValueField(FieldMatcher.newBuilder()
                                .setField(Atom.APP_BREADCRUMB_REPORTED_FIELD_NUMBER)
                                .addChild(FieldMatcher.newBuilder()
                                        .setField(AppBreadcrumbReported.LABEL_FIELD_NUMBER))
                        )
                        .setDimensionsInWhat(FieldMatcher.newBuilder().setField(whatMatcherId))
                )
                .addMetricActivation(MetricActivation.newBuilder()
                        .setMetricId(MetricsUtils.VALUE_METRIC_ID)
                        .addEventActivation(EventActivation.newBuilder()
                                .setAtomMatcherId(activationMatcherId)
                                .setActivationType(ActivationType.ACTIVATE_IMMEDIATELY)
                                .setTtlSeconds(ttlSec)
                        )
                );

        ConfigUtils.uploadConfig(getDevice(), builder);

        // Activate the metric.
        AtomTestUtils.sendAppBreadcrumbReportedAtom(getDevice(),
                AppBreadcrumbReported.State.START.getNumber(), activationMatcherLabel);
        RunUtil.getDefault().sleep(10);

        // Set the condition to true.
        AtomTestUtils.sendAppBreadcrumbReportedAtom(getDevice(),
                AppBreadcrumbReported.State.START.getNumber(), conditionLabel);
        RunUtil.getDefault().sleep(10);

        // Skipped due to unknown condition at start of bucket.
        AtomTestUtils.sendAppBreadcrumbReportedAtom(getDevice(),
                AppBreadcrumbReported.State.UNSPECIFIED.getNumber(), 10);
        RunUtil.getDefault().sleep(10);

        // Skipped due to unknown condition at start of bucket.
        AtomTestUtils.sendAppBreadcrumbReportedAtom(getDevice(),
                AppBreadcrumbReported.State.UNSPECIFIED.getNumber(), 200);
        RunUtil.getDefault().sleep(10);

        // Set the condition to false.
        AtomTestUtils.sendAppBreadcrumbReportedAtom(getDevice(),
                AppBreadcrumbReported.State.STOP.getNumber(), conditionLabel);
        RunUtil.getDefault().sleep(10);

        // Log an event that should not be counted because condition is false.
        AtomTestUtils.sendAppBreadcrumbReportedAtom(getDevice(),
                AppBreadcrumbReported.State.UNSPECIFIED.getNumber(), 3_000);
        RunUtil.getDefault().sleep(10);

        // Let the metric deactivate.
        RunUtil.getDefault().sleep(ttlSec * 1000);

        // Log an event that should not be counted.
        AtomTestUtils.sendAppBreadcrumbReportedAtom(getDevice(),
                AppBreadcrumbReported.State.UNSPECIFIED.getNumber(), 40_000);
        RunUtil.getDefault().sleep(10);

        // Condition to true again.
        AtomTestUtils.sendAppBreadcrumbReportedAtom(getDevice(),
                AppBreadcrumbReported.State.START.getNumber(), conditionLabel);
        RunUtil.getDefault().sleep(10);

        // Event should not be counted, metric is still not active.
        AtomTestUtils.sendAppBreadcrumbReportedAtom(getDevice(),
                AppBreadcrumbReported.State.UNSPECIFIED.getNumber(), 500_000);
        RunUtil.getDefault().sleep(10);

        // Activate the metric.
        AtomTestUtils.sendAppBreadcrumbReportedAtom(getDevice(),
                AppBreadcrumbReported.State.START.getNumber(), activationMatcherLabel);
        RunUtil.getDefault().sleep(10);

        //  Log an event that should be counted.
        AtomTestUtils.sendAppBreadcrumbReportedAtom(getDevice(),
                AppBreadcrumbReported.State.UNSPECIFIED.getNumber(), 6_000_000);
        RunUtil.getDefault().sleep(10);

        // Let the metric deactivate.
        RunUtil.getDefault().sleep(ttlSec * 1000);

        // Log an event that should not be counted.
        AtomTestUtils.sendAppBreadcrumbReportedAtom(getDevice(),
                AppBreadcrumbReported.State.UNSPECIFIED.getNumber(), 70_000_000);
        RunUtil.getDefault().sleep(10);

        // Wait for the metrics to propagate to statsd.
        RunUtil.getDefault().sleep(2000);

        StatsLogReport metricReport = ReportUtils.getStatsLogReport(getDevice(),
                ExtensionRegistry.getEmptyRegistry());
        LogUtil.CLog.d("Received the following data: " + metricReport.toString());
        assertThat(metricReport.getMetricId()).isEqualTo(MetricsUtils.VALUE_METRIC_ID);
        assertThat(metricReport.hasValueMetrics()).isTrue();
        assertThat(metricReport.getIsActive()).isFalse();

        StatsLogReport.ValueMetricDataWrapper valueData = metricReport.getValueMetrics();
        assertThat(valueData.getDataCount()).isEqualTo(1);
        assertThat(valueData.getData(0).getBucketInfoCount()).isEqualTo(1);
        long totalValue = valueData.getData(0).getBucketInfoList().stream()
                .peek(MetricsUtils::assertBucketTimePresent)
                .peek(bucketInfo -> assertThat(bucketInfo.getValuesCount()).isEqualTo(1))
                .map(bucketInfo -> bucketInfo.getValues(0))
                .peek(value -> assertThat(value.getIndex()).isEqualTo(0))
                .mapToLong(value -> value.getValueLong())
                .sum();
        assertThat(totalValue).isEqualTo(6_000_000);
    }

}
