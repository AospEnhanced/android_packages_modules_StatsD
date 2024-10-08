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
package android.cts.statsd.validation;

import static com.google.common.truth.Truth.assertThat;
import static com.google.common.truth.Truth.assertWithMessage;

import android.cts.statsd.metric.MetricsUtils;
import android.cts.statsdatom.lib.AtomTestUtils;
import android.cts.statsdatom.lib.ConfigUtils;
import android.cts.statsdatom.lib.DeviceUtils;
import android.cts.statsdatom.lib.ReportUtils;
import android.os.BatteryPluggedStateEnum;
import android.os.BatteryStatsProto;
import android.os.UidProto;
import android.os.UidProto.Wakelock;
import android.os.WakeLockLevelEnum;
import android.platform.test.annotations.RestrictedBuildTest;
import android.view.DisplayStateEnum;

import com.android.internal.os.StatsdConfigProto.AtomMatcher;
import com.android.internal.os.StatsdConfigProto.DurationMetric;
import com.android.internal.os.StatsdConfigProto.FieldMatcher;
import com.android.internal.os.StatsdConfigProto.FieldValueMatcher;
import com.android.internal.os.StatsdConfigProto.LogicalOperation;
import com.android.internal.os.StatsdConfigProto.Position;
import com.android.internal.os.StatsdConfigProto.Predicate;
import com.android.internal.os.StatsdConfigProto.SimpleAtomMatcher;
import com.android.internal.os.StatsdConfigProto.SimplePredicate;
import com.android.internal.os.StatsdConfigProto.StatsdConfig;
import com.android.internal.os.StatsdConfigProto.TimeUnit;
import com.android.os.AtomsProto.Atom;
import com.android.os.AtomsProto.PluggedStateChanged;
import com.android.os.AtomsProto.ScreenStateChanged;
import com.android.os.AtomsProto.WakelockStateChanged;
import com.android.os.StatsLog.DimensionsValue;
import com.android.os.StatsLog.DurationBucketInfo;
import com.android.os.StatsLog.DurationMetricData;
import com.android.os.StatsLog.EventMetricData;
import com.android.os.StatsLog.StatsLogReport;
import com.android.tradefed.build.IBuildInfo;
import com.android.tradefed.log.LogUtil.CLog;
import com.android.tradefed.testtype.DeviceTestCase;
import com.android.tradefed.testtype.IBuildReceiver;
import com.android.tradefed.util.RunUtil;

import com.google.common.collect.Range;
import com.google.protobuf.ExtensionRegistry;

import java.util.Arrays;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

/**
 * Side-by-side comparison between statsd and batterystats.
 */
public class ValidationTests extends DeviceTestCase implements IBuildReceiver {

    private IBuildInfo mCtsBuild;

    @Override
    protected void setUp() throws Exception {
        super.setUp();
        assertThat(mCtsBuild).isNotNull();
        ConfigUtils.removeConfig(getDevice());
        ReportUtils.clearReports(getDevice());
        DeviceUtils.installTestApp(getDevice(), MetricsUtils.DEVICE_SIDE_TEST_APK,
                MetricsUtils.DEVICE_SIDE_TEST_PACKAGE, mCtsBuild);
        RunUtil.getDefault().sleep(1000);
        DeviceUtils.turnBatteryStatsAutoResetOff(
                getDevice()); // Turn off Battery Stats auto resetting
    }

    @Override
    protected void tearDown() throws Exception {
        ConfigUtils.removeConfig(getDevice());
        ReportUtils.clearReports(getDevice());
        DeviceUtils.uninstallTestApp(getDevice(), MetricsUtils.DEVICE_SIDE_TEST_PACKAGE);
        DeviceUtils.resetBatteryStatus(getDevice());
        DeviceUtils.turnScreenOn(getDevice());
        DeviceUtils.turnBatteryStatsAutoResetOn(getDevice());
        super.tearDown();
    }

    @Override
    public void setBuild(IBuildInfo buildInfo) {
        mCtsBuild = buildInfo;
    }

    private static final String TAG = "Statsd.ValidationTests";
    private static final String FEATURE_AUTOMOTIVE = "android.hardware.type.automotive";
    private static final boolean ENABLE_LOAD_TEST = false;

    public void testPartialWakelock() throws Exception {
        if (!DeviceUtils.hasFeature(getDevice(), FEATURE_AUTOMOTIVE)) return;
        resetBatteryStats();
        DeviceUtils.unplugDevice(getDevice());
        DeviceUtils.flushBatteryStatsHandlers(getDevice());
        // AoD needs to be turned off because the screen should go into an off state. But, if AoD is
        // on and the device doesn't support STATE_DOZE, the screen sadly goes back to STATE_ON.
        String aodState = DeviceUtils.getAodState(getDevice());
        DeviceUtils.setAodState(getDevice(), "0");
        DeviceUtils.turnScreenOff(getDevice());

        final int atomTag = Atom.WAKELOCK_STATE_CHANGED_FIELD_NUMBER;
        Set<Integer> wakelockOn = new HashSet<>(Arrays.asList(
                WakelockStateChanged.State.ACQUIRE_VALUE,
                WakelockStateChanged.State.CHANGE_ACQUIRE_VALUE));
        Set<Integer> wakelockOff = new HashSet<>(Arrays.asList(
                WakelockStateChanged.State.RELEASE_VALUE,
                WakelockStateChanged.State.CHANGE_RELEASE_VALUE));

        final String EXPECTED_TAG = "StatsdPartialWakelock";
        final WakeLockLevelEnum EXPECTED_LEVEL = WakeLockLevelEnum.PARTIAL_WAKE_LOCK;

        // Add state sets to the list in order.
        List<Set<Integer>> stateSet = Arrays.asList(wakelockOn, wakelockOff);

        ConfigUtils.uploadConfigForPushedAtomWithUid(getDevice(),
                MetricsUtils.DEVICE_SIDE_TEST_PACKAGE, atomTag, true);  // True: uses attribution.
        DeviceUtils.runDeviceTests(getDevice(), MetricsUtils.DEVICE_SIDE_TEST_PACKAGE, ".AtomTests",
                "testWakelockState");

        // Sorted list of events in order in which they occurred.
        List<EventMetricData> data = ReportUtils.getEventMetricDataList(getDevice());

        //=================== verify that statsd is correct ===============//
        // Assert that the events happened in the expected order.
        AtomTestUtils.assertStatesOccurred(stateSet, data,
                atom -> atom.getWakelockStateChanged().getState().getNumber());

        for (EventMetricData event : data) {
            String tag = event.getAtom().getWakelockStateChanged().getTag();
            WakeLockLevelEnum type = event.getAtom().getWakelockStateChanged().getType();
            assertThat(tag).isEqualTo(EXPECTED_TAG);
            assertThat(type).isEqualTo(EXPECTED_LEVEL);
        }
    }

    @RestrictedBuildTest
    public void testPartialWakelockDuration() throws Exception {
        if (!DeviceUtils.hasFeature(getDevice(), FEATURE_AUTOMOTIVE)) return;

        // getUid() needs shell command via ADB. turnScreenOff() sometimes let system go to suspend.
        // ADB disconnection causes failure of getUid(). Move up here before turnScreenOff().
        final int EXPECTED_UID = DeviceUtils.getAppUid(getDevice(),
                MetricsUtils.DEVICE_SIDE_TEST_PACKAGE);

        DeviceUtils.turnScreenOn(getDevice()); // To ensure that the ScreenOff later gets logged.
        // AoD needs to be turned off because the screen should go into an off state. But, if AoD is
        // on and the device doesn't support STATE_DOZE, the screen sadly goes back to STATE_ON.
        String aodState = DeviceUtils.getAodState(getDevice());
        DeviceUtils.setAodState(getDevice(), "0");
        uploadWakelockDurationBatteryStatsConfig(TimeUnit.CTS);
        RunUtil.getDefault().sleep(AtomTestUtils.WAIT_TIME_SHORT);
        resetBatteryStats();
        DeviceUtils.unplugDevice(getDevice());
        DeviceUtils.turnScreenOff(getDevice());
        DeviceUtils.flushBatteryStatsHandlers(getDevice());

        RunUtil.getDefault().sleep(AtomTestUtils.WAIT_TIME_SHORT);

        DeviceUtils.runDeviceTests(getDevice(), MetricsUtils.DEVICE_SIDE_TEST_PACKAGE, ".AtomTests",
                "testWakelockState");
        RunUtil.getDefault().sleep(AtomTestUtils.WAIT_TIME_LONG); // Make sure the one second bucket has ended.


        final String EXPECTED_TAG = "StatsdPartialWakelock";
        final long EXPECTED_TAG_HASH = Long.parseUnsignedLong("15814523794762874414");
        final int MIN_DURATION = 350;
        final int MAX_DURATION = 700;

        HashMap<Integer, HashMap<Long, Long>> statsdWakelockData = getStatsdWakelockData();

        // Get the statsd wakelock time and make sure it's reasonable.
        assertWithMessage("No wakelocks with uid %s in statsd", EXPECTED_UID)
                .that(statsdWakelockData).containsKey(EXPECTED_UID);
        assertWithMessage("No wakelocks with tag %s in statsd", EXPECTED_TAG)
                .that(statsdWakelockData.get(EXPECTED_UID)).containsKey(EXPECTED_TAG_HASH);
        long statsdDurationMs = statsdWakelockData.get(EXPECTED_UID)
                .get(EXPECTED_TAG_HASH) / 1_000_000;
        assertWithMessage(
                "Wakelock in statsd with uid %s and tag %s was too short or too long",
                EXPECTED_UID, EXPECTED_TAG
        ).that(statsdDurationMs).isIn(Range.closed((long) MIN_DURATION, (long) MAX_DURATION));

        DeviceUtils.setAodState(getDevice(), aodState); // restores AOD to initial state.
    }

    public void testPartialWakelockLoad() throws Exception {
        if (!ENABLE_LOAD_TEST) return;
        DeviceUtils.turnScreenOn(getDevice()); // To ensure that the ScreenOff later gets logged.
        uploadWakelockDurationBatteryStatsConfig(TimeUnit.CTS);
        RunUtil.getDefault().sleep(AtomTestUtils.WAIT_TIME_SHORT);
        resetBatteryStats();
        DeviceUtils.unplugDevice(getDevice());
        DeviceUtils.turnScreenOff(getDevice());

        DeviceUtils.runDeviceTests(getDevice(), MetricsUtils.DEVICE_SIDE_TEST_PACKAGE, ".AtomTests",
                "testWakelockLoad");
        // Give time for stuck wakelocks to increase duration.
        RunUtil.getDefault().sleep(10_000);


        final String EXPECTED_TAG = "StatsdPartialWakelock";
        final int EXPECTED_UID = DeviceUtils.getAppUid(getDevice(),
                MetricsUtils.DEVICE_SIDE_TEST_PACKAGE);
        final int NUM_THREADS = 16;
        final int NUM_COUNT_PER_THREAD = 1000;
        final int MAX_DURATION_MS = 15_000;
        final int MIN_DURATION_MS = 1_000;


//        BatteryStatsProto batterystatsProto = getBatteryStatsProto();
//        HashMap<Integer, HashMap<Long, Long>> statsdWakelockData = getStatsdWakelockData();

        // TODO: this fails because we only have the hashes of the wakelock tags in statsd.
        // If we want to run this test, we need to fix this.

        // Verify batterystats output is reasonable.
        // boolean foundUid = false;
        // for (UidProto uidProto : batterystatsProto.getUidsList()) {
        //     if (uidProto.getUid() == EXPECTED_UID) {
        //         foundUid = true;
        //         CLog.d("Battery stats has the following wakelocks: \n" +
        //                 uidProto.getWakelocksList());
        //         assertTrue("UidProto has size "  + uidProto.getWakelocksList().size() +
        //                 " wakelocks in it. Expected " + NUM_THREADS + " wakelocks.",
        //                 uidProto.getWakelocksList().size() == NUM_THREADS);
        //
        //         for (Wakelock wl : uidProto.getWakelocksList()) {
        //             String tag = wl.getName();
        //             assertTrue("Wakelock tag in batterystats " + tag + " does not contain "
        //                     + "expected tag " + EXPECTED_TAG, tag.contains(EXPECTED_TAG));
        //             assertTrue("Wakelock in batterystats with tag " + tag + " does not have any "
        //                             + "partial wakelock data.", wl.hasPartial());
        //             assertTrue("Wakelock in batterystats with tag " + tag + " tag has count " +
        //                     wl.getPartial().getCount() + " Expected " + NUM_COUNT_PER_THREAD,
        //                     wl.getPartial().getCount() == NUM_COUNT_PER_THREAD);
        //             long bsDurationMs = wl.getPartial().getTotalDurationMs();
        //             assertTrue("Wakelock in batterystats with uid " + EXPECTED_UID + " and tag "
        //                     + EXPECTED_TAG + "was too short. Expected " + MIN_DURATION_MS +
        //                     ", received " + bsDurationMs, bsDurationMs >= MIN_DURATION_MS);
        //             assertTrue("Wakelock in batterystats with uid " + EXPECTED_UID + " and tag "
        //                     + EXPECTED_TAG + "was too long. Expected " + MAX_DURATION_MS +
        //                     ", received " + bsDurationMs, bsDurationMs <= MAX_DURATION_MS);
        //
        //             // Validate statsd.
        //             long statsdDurationNs = statsdWakelockData.get(EXPECTED_UID).get(tag);
        //             long statsdDurationMs = statsdDurationNs / 1_000_000;
        //             long difference = Math.abs(statsdDurationMs - bsDurationMs);
        //             assertTrue("Unusually large difference in wakelock duration for tag: " +
        // tag +
        //                         ". Statsd had duration " + statsdDurationMs +
        //                         " and batterystats had duration " + bsDurationMs,
        //                         difference <= bsDurationMs / 10);
        //
        //         }
        //     }
        // }
        // assertTrue("Did not find uid " + EXPECTED_UID + " in batterystats.", foundUid);
        //
        // // Assert that the wakelock appears in statsd and is correct.
        // assertTrue("Could not find any wakelocks with uid " + EXPECTED_UID + " in statsd",
        //         statsdWakelockData.containsKey(EXPECTED_UID));
        // HashMap<String, Long> expectedWakelocks = statsdWakelockData.get(EXPECTED_UID);
        // assertEquals("Expected " + NUM_THREADS + " wakelocks in statsd with UID " +
        // EXPECTED_UID +
        //         ". Received " + expectedWakelocks.size(), expectedWakelocks.size(), NUM_THREADS);
    }

    // Helper functions
    // TODO: Refactor these into some utils class.

    public HashMap<Integer, HashMap<Long, Long>> getStatsdWakelockData() throws Exception {
        StatsLogReport report = ReportUtils.getStatsLogReport(getDevice(),
                ExtensionRegistry.getEmptyRegistry());
        CLog.d("Received the following stats log report: \n" + report.toString());

        // Stores total duration of each wakelock across buckets.
        HashMap<Integer, HashMap<Long, Long>> statsdWakelockData = new HashMap<>();

        for (DurationMetricData data : report.getDurationMetrics().getDataList()) {
            // Gets tag and uid.
            List<DimensionsValue> dims = data.getDimensionLeafValuesInWhatList();
            assertThat(dims).hasSize(2);
            boolean hasTag = false;
            long tag = 0;
            int uid = -1;
            long duration = 0;
            for (DimensionsValue dim : dims) {
                if (dim.hasValueInt()) {
                    uid = dim.getValueInt();
                } else if (dim.hasValueStrHash()) {
                    hasTag = true;
                    tag = dim.getValueStrHash();
                }
            }
            assertWithMessage("Did not receive a tag for the wakelock").that(hasTag).isTrue();
            assertWithMessage("Did not receive a uid for the wakelock").that(uid).isNotEqualTo(-1);

            // Gets duration.
            for (DurationBucketInfo bucketInfo : data.getBucketInfoList()) {
                duration += bucketInfo.getDurationNanos();
            }

            // Store the info.
            if (statsdWakelockData.containsKey(uid)) {
                HashMap<Long, Long> tagToDuration = statsdWakelockData.get(uid);
                tagToDuration.put(tag, duration);
            } else {
                HashMap<Long, Long> tagToDuration = new HashMap<>();
                tagToDuration.put(tag, duration);
                statsdWakelockData.put(uid, tagToDuration);
            }
        }
        CLog.d("follow: statsdwakelockdata is: " + statsdWakelockData);
        return statsdWakelockData;
    }

    private android.os.TimerProto getBatteryStatsPartialWakelock(BatteryStatsProto proto,
            long uid, String tag) {
        if (proto.getUidsList().size() < 1) {
            CLog.w("Batterystats proto contains no uids");
            return null;
        }
        boolean hadUid = false;
        for (UidProto uidProto : proto.getUidsList()) {
            if (uidProto.getUid() == uid) {
                hadUid = true;
                for (Wakelock wl : uidProto.getWakelocksList()) {
                    if (tag.equals(wl.getName())) {
                        if (wl.hasPartial()) {
                            return wl.getPartial();
                        }
                        CLog.w("Batterystats had wakelock for uid (" + uid + ") "
                                + "with tag (" + tag + ") "
                                + "but it didn't have a partial wakelock");
                    }
                }
                CLog.w("Batterystats didn't have a partial wakelock for uid " + uid
                        + " with tag " + tag);
            }
        }
        if (!hadUid) CLog.w("Batterystats didn't have uid " + uid);
        return null;
    }

    public void uploadWakelockDurationBatteryStatsConfig(TimeUnit bucketsize) throws Exception {
        final int atomTag = Atom.WAKELOCK_STATE_CHANGED_FIELD_NUMBER;
        String metricName = "DURATION_PARTIAL_WAKELOCK_PER_TAG_UID_WHILE_SCREEN_OFF_ON_BATTERY";
        int metricId = metricName.hashCode();

        String partialWakelockIsOnName = "PARTIAL_WAKELOCK_IS_ON";
        int partialWakelockIsOnId = partialWakelockIsOnName.hashCode();

        String partialWakelockOnName = "PARTIAL_WAKELOCK_ON";
        int partialWakelockOnId = partialWakelockOnName.hashCode();
        String partialWakelockOffName = "PARTIAL_WAKELOCK_OFF";
        int partialWakelockOffId = partialWakelockOffName.hashCode();

        String partialWakelockAcquireName = "PARTIAL_WAKELOCK_ACQUIRE";
        int partialWakelockAcquireId = partialWakelockAcquireName.hashCode();
        String partialWakelockChangeAcquireName = "PARTIAL_WAKELOCK_CHANGE_ACQUIRE";
        int partialWakelockChangeAcquireId = partialWakelockChangeAcquireName.hashCode();

        String partialWakelockReleaseName = "PARTIAL_WAKELOCK_RELEASE";
        int partialWakelockReleaseId = partialWakelockReleaseName.hashCode();
        String partialWakelockChangeReleaseName = "PARTIAL_WAKELOCK_CHANGE_RELEASE";
        int partialWakelockChangeReleaseId = partialWakelockChangeReleaseName.hashCode();


        String screenOffBatteryOnName = "SCREEN_IS_OFF_ON_BATTERY";
        int screenOffBatteryOnId = screenOffBatteryOnName.hashCode();

        String screenStateUnknownName = "SCREEN_STATE_UNKNOWN";
        int screenStateUnknownId = screenStateUnknownName.hashCode();
        String screenStateOffName = "SCREEN_STATE_OFF";
        int screenStateOffId = screenStateOffName.hashCode();
        String screenStateOnName = "SCREEN_STATE_ON";
        int screenStateOnId = screenStateOnName.hashCode();
        String screenStateDozeName = "SCREEN_STATE_DOZE";
        int screenStateDozeId = screenStateDozeName.hashCode();
        String screenStateDozeSuspendName = "SCREEN_STATE_DOZE_SUSPEND";
        int screenStateDozeSuspendId = screenStateDozeSuspendName.hashCode();
        String screenStateVrName = "SCREEN_STATE_VR";
        int screenStateVrId = screenStateVrName.hashCode();
        String screenStateOnSuspendName = "SCREEN_STATE_ON_SUSPEND";
        int screenStateOnSuspendId = screenStateOnSuspendName.hashCode();

        String screenTurnedOnName = "SCREEN_TURNED_ON";
        int screenTurnedOnId = screenTurnedOnName.hashCode();
        String screenTurnedOffName = "SCREEN_TURNED_OFF";
        int screenTurnedOffId = screenTurnedOffName.hashCode();

        String screenIsOffName = "SCREEN_IS_OFF";
        int screenIsOffId = screenIsOffName.hashCode();

        String pluggedStateBatteryPluggedNoneName = "PLUGGED_STATE_BATTERY_PLUGGED_NONE";
        int pluggedStateBatteryPluggedNoneId = pluggedStateBatteryPluggedNoneName.hashCode();
        String pluggedStateBatteryPluggedAcName = "PLUGGED_STATE_BATTERY_PLUGGED_AC";
        int pluggedStateBatteryPluggedAcId = pluggedStateBatteryPluggedAcName.hashCode();
        String pluggedStateBatteryPluggedUsbName = "PLUGGED_STATE_BATTERY_PLUGGED_USB";
        int pluggedStateBatteryPluggedUsbId = pluggedStateBatteryPluggedUsbName.hashCode();
        String pluggedStateBatteryPluggedWlName = "PLUGGED_STATE_BATTERY_PLUGGED_WIRELESS";
        int pluggedStateBatteryPluggedWirelessId = pluggedStateBatteryPluggedWlName.hashCode();

        String pluggedStateBatteryPluggedName = "PLUGGED_STATE_BATTERY_PLUGGED";
        int pluggedStateBatteryPluggedId = pluggedStateBatteryPluggedName.hashCode();

        String deviceIsUnpluggedName = "DEVICE_IS_UNPLUGGED";
        int deviceIsUnpluggedId = deviceIsUnpluggedName.hashCode();


        FieldMatcher.Builder dimensions = FieldMatcher.newBuilder()
                .setField(atomTag)
                .addChild(FieldMatcher.newBuilder()
                        .setField(WakelockStateChanged.TAG_FIELD_NUMBER))
                .addChild(FieldMatcher.newBuilder()
                        .setField(1)
                        .setPosition(Position.FIRST)
                        .addChild(FieldMatcher.newBuilder()
                                .setField(1)));

        AtomMatcher.Builder wakelockAcquire = AtomMatcher.newBuilder()
                .setId(partialWakelockAcquireId)
                .setSimpleAtomMatcher(SimpleAtomMatcher.newBuilder()
                        .setAtomId(atomTag)
                        .addFieldValueMatcher(FieldValueMatcher.newBuilder()
                                .setField(WakelockStateChanged.TYPE_FIELD_NUMBER)
                                .setEqInt(WakeLockLevelEnum.PARTIAL_WAKE_LOCK_VALUE))
                        .addFieldValueMatcher(FieldValueMatcher.newBuilder()
                                .setField(WakelockStateChanged.STATE_FIELD_NUMBER)
                                .setEqInt(WakelockStateChanged.State.ACQUIRE_VALUE)));

        AtomMatcher.Builder wakelockChangeAcquire = AtomMatcher.newBuilder()
                .setId(partialWakelockChangeAcquireId)
                .setSimpleAtomMatcher(SimpleAtomMatcher.newBuilder()
                        .setAtomId(atomTag)
                        .addFieldValueMatcher(FieldValueMatcher.newBuilder()
                                .setField(WakelockStateChanged.TYPE_FIELD_NUMBER)
                                .setEqInt(WakeLockLevelEnum.PARTIAL_WAKE_LOCK_VALUE))
                        .addFieldValueMatcher(FieldValueMatcher.newBuilder()
                                .setField(WakelockStateChanged.STATE_FIELD_NUMBER)
                                .setEqInt(WakelockStateChanged.State.CHANGE_ACQUIRE_VALUE)));

        AtomMatcher.Builder wakelockRelease = AtomMatcher.newBuilder()
                .setId(partialWakelockReleaseId)
                .setSimpleAtomMatcher(SimpleAtomMatcher.newBuilder()
                        .setAtomId(atomTag)
                        .addFieldValueMatcher(FieldValueMatcher.newBuilder()
                                .setField(WakelockStateChanged.TYPE_FIELD_NUMBER)
                                .setEqInt(WakeLockLevelEnum.PARTIAL_WAKE_LOCK_VALUE))
                        .addFieldValueMatcher(FieldValueMatcher.newBuilder()
                                .setField(WakelockStateChanged.STATE_FIELD_NUMBER)
                                .setEqInt(WakelockStateChanged.State.RELEASE_VALUE)));

        AtomMatcher.Builder wakelockChangeRelease = AtomMatcher.newBuilder()
                .setId(partialWakelockChangeReleaseId)
                .setSimpleAtomMatcher(SimpleAtomMatcher.newBuilder()
                        .setAtomId(atomTag)
                        .addFieldValueMatcher(FieldValueMatcher.newBuilder()
                                .setField(WakelockStateChanged.TYPE_FIELD_NUMBER)
                                .setEqInt(WakeLockLevelEnum.PARTIAL_WAKE_LOCK_VALUE))
                        .addFieldValueMatcher(FieldValueMatcher.newBuilder()
                                .setField(WakelockStateChanged.STATE_FIELD_NUMBER)
                                .setEqInt(WakelockStateChanged.State.CHANGE_RELEASE_VALUE)));

        AtomMatcher.Builder wakelockOn = AtomMatcher.newBuilder()
                .setId(partialWakelockOnId)
                .setCombination(AtomMatcher.Combination.newBuilder()
                        .setOperation(LogicalOperation.OR)
                        .addMatcher(partialWakelockAcquireId)
                        .addMatcher(partialWakelockChangeAcquireId));

        AtomMatcher.Builder wakelockOff = AtomMatcher.newBuilder()
                .setId(partialWakelockOffId)
                .setCombination(AtomMatcher.Combination.newBuilder()
                        .setOperation(LogicalOperation.OR)
                        .addMatcher(partialWakelockReleaseId)
                        .addMatcher(partialWakelockChangeReleaseId));


        Predicate.Builder wakelockPredicate = Predicate.newBuilder()
                .setId(partialWakelockIsOnId)
                .setSimplePredicate(SimplePredicate.newBuilder()
                        .setStart(partialWakelockOnId)
                        .setStop(partialWakelockOffId)
                        .setCountNesting(true)
                        .setDimensions(dimensions));

        AtomMatcher.Builder pluggedStateBatteryPluggedNone = AtomMatcher.newBuilder()
                .setId(pluggedStateBatteryPluggedNoneId)
                .setSimpleAtomMatcher(SimpleAtomMatcher.newBuilder()
                        .setAtomId(Atom.PLUGGED_STATE_CHANGED_FIELD_NUMBER)
                        .addFieldValueMatcher(FieldValueMatcher.newBuilder()
                                .setField(PluggedStateChanged.STATE_FIELD_NUMBER)
                                .setEqInt(BatteryPluggedStateEnum.BATTERY_PLUGGED_NONE_VALUE)));

        AtomMatcher.Builder pluggedStateBatteryPluggedAc = AtomMatcher.newBuilder()
                .setId(pluggedStateBatteryPluggedAcId)
                .setSimpleAtomMatcher(SimpleAtomMatcher.newBuilder()
                        .setAtomId(Atom.PLUGGED_STATE_CHANGED_FIELD_NUMBER)
                        .addFieldValueMatcher(FieldValueMatcher.newBuilder()
                                .setField(PluggedStateChanged.STATE_FIELD_NUMBER)
                                .setEqInt(BatteryPluggedStateEnum.BATTERY_PLUGGED_AC_VALUE)));

        AtomMatcher.Builder pluggedStateBatteryPluggedUsb = AtomMatcher.newBuilder()
                .setId(pluggedStateBatteryPluggedUsbId)
                .setSimpleAtomMatcher(SimpleAtomMatcher.newBuilder()
                        .setAtomId(Atom.PLUGGED_STATE_CHANGED_FIELD_NUMBER)
                        .addFieldValueMatcher(FieldValueMatcher.newBuilder()
                                .setField(PluggedStateChanged.STATE_FIELD_NUMBER)
                                .setEqInt(BatteryPluggedStateEnum.BATTERY_PLUGGED_USB_VALUE)));

        AtomMatcher.Builder pluggedStateBatteryPluggedWireless = AtomMatcher.newBuilder()
                .setId(pluggedStateBatteryPluggedWirelessId)
                .setSimpleAtomMatcher(SimpleAtomMatcher.newBuilder()
                        .setAtomId(Atom.PLUGGED_STATE_CHANGED_FIELD_NUMBER)
                        .addFieldValueMatcher(FieldValueMatcher.newBuilder()
                                .setField(PluggedStateChanged.STATE_FIELD_NUMBER)
                                .setEqInt(BatteryPluggedStateEnum.BATTERY_PLUGGED_WIRELESS_VALUE)));

        AtomMatcher.Builder pluggedStateBatteryPlugged = AtomMatcher.newBuilder()
                .setId(pluggedStateBatteryPluggedId)
                .setCombination(AtomMatcher.Combination.newBuilder()
                        .setOperation(LogicalOperation.OR)
                        .addMatcher(pluggedStateBatteryPluggedAcId)
                        .addMatcher(pluggedStateBatteryPluggedUsbId)
                        .addMatcher(pluggedStateBatteryPluggedWirelessId));

        Predicate.Builder deviceIsUnplugged = Predicate.newBuilder()
                .setId(deviceIsUnpluggedId)
                .setSimplePredicate(SimplePredicate.newBuilder()
                        .setStart(pluggedStateBatteryPluggedNoneId)
                        .setStop(pluggedStateBatteryPluggedId)
                        .setCountNesting(false));

        AtomMatcher.Builder screenStateUnknown = AtomMatcher.newBuilder()
                .setId(screenStateUnknownId)
                .setSimpleAtomMatcher(SimpleAtomMatcher.newBuilder()
                        .setAtomId(Atom.SCREEN_STATE_CHANGED_FIELD_NUMBER)
                        .addFieldValueMatcher(FieldValueMatcher.newBuilder()
                                .setField(ScreenStateChanged.STATE_FIELD_NUMBER)
                                .setEqInt(DisplayStateEnum.DISPLAY_STATE_UNKNOWN_VALUE)));

        AtomMatcher.Builder screenStateOff = AtomMatcher.newBuilder()
                .setId(screenStateOffId)
                .setSimpleAtomMatcher(SimpleAtomMatcher.newBuilder()
                        .setAtomId(Atom.SCREEN_STATE_CHANGED_FIELD_NUMBER)
                        .addFieldValueMatcher(FieldValueMatcher.newBuilder()
                                .setField(ScreenStateChanged.STATE_FIELD_NUMBER)
                                .setEqInt(DisplayStateEnum.DISPLAY_STATE_OFF_VALUE)));

        AtomMatcher.Builder screenStateOn = AtomMatcher.newBuilder()
                .setId(screenStateOnId)
                .setSimpleAtomMatcher(SimpleAtomMatcher.newBuilder()
                        .setAtomId(Atom.SCREEN_STATE_CHANGED_FIELD_NUMBER)
                        .addFieldValueMatcher(FieldValueMatcher.newBuilder()
                                .setField(ScreenStateChanged.STATE_FIELD_NUMBER)
                                .setEqInt(DisplayStateEnum.DISPLAY_STATE_ON_VALUE)));

        AtomMatcher.Builder screenStateDoze = AtomMatcher.newBuilder()
                .setId(screenStateDozeId)
                .setSimpleAtomMatcher(SimpleAtomMatcher.newBuilder()
                        .setAtomId(Atom.SCREEN_STATE_CHANGED_FIELD_NUMBER)
                        .addFieldValueMatcher(FieldValueMatcher.newBuilder()
                                .setField(ScreenStateChanged.STATE_FIELD_NUMBER)
                                .setEqInt(DisplayStateEnum.DISPLAY_STATE_DOZE_VALUE)));

        AtomMatcher.Builder screenStateDozeSuspend = AtomMatcher.newBuilder()
                .setId(screenStateDozeSuspendId)
                .setSimpleAtomMatcher(SimpleAtomMatcher.newBuilder()
                        .setAtomId(Atom.SCREEN_STATE_CHANGED_FIELD_NUMBER)
                        .addFieldValueMatcher(FieldValueMatcher.newBuilder()
                                .setField(ScreenStateChanged.STATE_FIELD_NUMBER)
                                .setEqInt(DisplayStateEnum.DISPLAY_STATE_DOZE_SUSPEND_VALUE)));

        AtomMatcher.Builder screenStateVr = AtomMatcher.newBuilder()
                .setId(screenStateVrId)
                .setSimpleAtomMatcher(SimpleAtomMatcher.newBuilder()
                        .setAtomId(Atom.SCREEN_STATE_CHANGED_FIELD_NUMBER)
                        .addFieldValueMatcher(FieldValueMatcher.newBuilder()
                                .setField(ScreenStateChanged.STATE_FIELD_NUMBER)
                                .setEqInt(DisplayStateEnum.DISPLAY_STATE_VR_VALUE)));

        AtomMatcher.Builder screenStateOnSuspend = AtomMatcher.newBuilder()
                .setId(screenStateOnSuspendId)
                .setSimpleAtomMatcher(SimpleAtomMatcher.newBuilder()
                        .setAtomId(Atom.SCREEN_STATE_CHANGED_FIELD_NUMBER)
                        .addFieldValueMatcher(FieldValueMatcher.newBuilder()
                                .setField(ScreenStateChanged.STATE_FIELD_NUMBER)
                                .setEqInt(DisplayStateEnum.DISPLAY_STATE_ON_SUSPEND_VALUE)));


        AtomMatcher.Builder screenTurnedOff = AtomMatcher.newBuilder()
                .setId(screenTurnedOffId)
                .setCombination(AtomMatcher.Combination.newBuilder()
                        .setOperation(LogicalOperation.OR)
                        .addMatcher(screenStateOffId)
                        .addMatcher(screenStateDozeId)
                        .addMatcher(screenStateDozeSuspendId)
                        .addMatcher(screenStateUnknownId));

        AtomMatcher.Builder screenTurnedOn = AtomMatcher.newBuilder()
                .setId(screenTurnedOnId)
                .setCombination(AtomMatcher.Combination.newBuilder()
                        .setOperation(LogicalOperation.OR)
                        .addMatcher(screenStateOnId)
                        .addMatcher(screenStateOnSuspendId)
                        .addMatcher(screenStateVrId));

        Predicate.Builder screenIsOff = Predicate.newBuilder()
                .setId(screenIsOffId)
                .setSimplePredicate(SimplePredicate.newBuilder()
                        .setStart(screenTurnedOffId)
                        .setStop(screenTurnedOnId)
                        .setCountNesting(false));


        Predicate.Builder screenOffBatteryOn = Predicate.newBuilder()
                .setId(screenOffBatteryOnId)
                .setCombination(Predicate.Combination.newBuilder()
                        .setOperation(LogicalOperation.AND)
                        .addPredicate(screenIsOffId)
                        .addPredicate(deviceIsUnpluggedId));

        StatsdConfig.Builder builder = ConfigUtils.createConfigBuilder(
                MetricsUtils.DEVICE_SIDE_TEST_PACKAGE);
        builder.addDurationMetric(DurationMetric.newBuilder()
                        .setId(metricId)
                        .setWhat(partialWakelockIsOnId)
                        .setCondition(screenOffBatteryOnId)
                        .setDimensionsInWhat(dimensions)
                        .setBucket(bucketsize))
                .addAtomMatcher(wakelockAcquire)
                .addAtomMatcher(wakelockChangeAcquire)
                .addAtomMatcher(wakelockRelease)
                .addAtomMatcher(wakelockChangeRelease)
                .addAtomMatcher(wakelockOn)
                .addAtomMatcher(wakelockOff)
                .addAtomMatcher(pluggedStateBatteryPluggedNone)
                .addAtomMatcher(pluggedStateBatteryPluggedAc)
                .addAtomMatcher(pluggedStateBatteryPluggedUsb)
                .addAtomMatcher(pluggedStateBatteryPluggedWireless)
                .addAtomMatcher(pluggedStateBatteryPlugged)
                .addAtomMatcher(screenStateUnknown)
                .addAtomMatcher(screenStateOff)
                .addAtomMatcher(screenStateOn)
                .addAtomMatcher(screenStateDoze)
                .addAtomMatcher(screenStateDozeSuspend)
                .addAtomMatcher(screenStateVr)
                .addAtomMatcher(screenStateOnSuspend)
                .addAtomMatcher(screenTurnedOff)
                .addAtomMatcher(screenTurnedOn)
                .addPredicate(wakelockPredicate)
                .addPredicate(deviceIsUnplugged)
                .addPredicate(screenIsOff)
                .addPredicate(screenOffBatteryOn);

        ConfigUtils.uploadConfig(getDevice(), builder);
    }

    private void resetBatteryStats() throws Exception {
        getDevice().executeShellCommand("dumpsys batterystats --reset");
    }
}
