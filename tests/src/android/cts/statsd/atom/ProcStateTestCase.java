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
package android.cts.statsd.atom;

import static com.google.common.truth.Truth.assertThat;

import android.app.ProcessStateEnum; // From enums.proto for atoms.proto's UidProcessStateChanged.
import android.cts.statsd.metric.MetricsUtils;
import android.cts.statsdatom.lib.ConfigUtils;
import android.cts.statsdatom.lib.DeviceUtils;
import android.cts.statsdatom.lib.ReportUtils;

import com.android.os.AtomsProto.Atom;
import com.android.os.StatsLog.EventMetricData;
import com.android.tradefed.build.IBuildInfo;
import com.android.tradefed.testtype.DeviceTestCase;
import com.android.tradefed.testtype.IBuildReceiver;
import com.android.tradefed.util.RunUtil;

import java.util.Arrays;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.function.Function;
import java.util.stream.Collectors;
import java.util.stream.Stream;

/**
 * Base class for manipulating process states
 */
public class ProcStateTestCase extends DeviceTestCase implements IBuildReceiver {

    private static final String TAG = "Statsd.ProcStateTestCase";

    private static final String DEVICE_SIDE_FG_ACTIVITY_COMPONENT
            = "com.android.server.cts.device.statsd/.StatsdCtsForegroundActivity";
    private static final String DEVICE_SIDE_FG_SERVICE_COMPONENT
            = "com.android.server.cts.device.statsd/.StatsdCtsForegroundService";

    private static final String KEY_ACTION = "action";

    // Constants from the device-side tests (not directly accessible here).
    public static final String ACTION_END_IMMEDIATELY = "action.end_immediately";
    public static final String ACTION_BACKGROUND_SLEEP = "action.background_sleep";
    public static final String ACTION_SLEEP_WHILE_TOP = "action.sleep_top";
    public static final String ACTION_LONG_SLEEP_WHILE_TOP = "action.long_sleep_top";
    public static final String ACTION_SHOW_APPLICATION_OVERLAY = "action.show_application_overlay";

    // Sleep times (ms) that actions invoke device-side.
    public static final int SLEEP_OF_ACTION_SLEEP_WHILE_TOP = 2_000;
    public static final int SLEEP_OF_ACTION_LONG_SLEEP_WHILE_TOP = 60_000;
    public static final int SLEEP_OF_ACTION_BACKGROUND_SLEEP = 2_000;
    public static final int SLEEP_OF_FOREGROUND_SERVICE = 2_000;

    protected IBuildInfo mCtsBuild;

    @Override
    protected void setUp() throws Exception {
        super.setUp();
        assertThat(mCtsBuild).isNotNull();
        ConfigUtils.removeConfig(getDevice());
        ReportUtils.clearReports(getDevice());
        DeviceUtils.installTestApp(getDevice(), MetricsUtils.DEVICE_SIDE_TEST_APK,
                MetricsUtils.DEVICE_SIDE_TEST_PACKAGE, mCtsBuild);
        RunUtil.getDefault().sleep(1000);
    }

    @Override
    protected void tearDown() throws Exception {
        ConfigUtils.removeConfig(getDevice());
        ReportUtils.clearReports(getDevice());
        DeviceUtils.uninstallTestApp(getDevice(), MetricsUtils.DEVICE_SIDE_TEST_PACKAGE);
        super.tearDown();
    }

    @Override
    public void setBuild(IBuildInfo buildInfo) {
        mCtsBuild = buildInfo;
    }

    /**
     * Runs an activity (in the foreground) to perform the given action.
     *
     * @param actionValue the action code constants indicating the desired action to perform.
     */
    protected void executeForegroundActivity(String actionValue) throws Exception {
        getDevice().executeShellCommand(String.format(
                "am start -n '%s' -e %s %s",
                DEVICE_SIDE_FG_ACTIVITY_COMPONENT,
                KEY_ACTION, actionValue));
    }

    /**
     * Runs a simple foreground service.
     */
    protected void executeForegroundService() throws Exception {
        executeForegroundActivity(ACTION_END_IMMEDIATELY);
        getDevice().executeShellCommand(String.format(
                "am startservice -n '%s'", DEVICE_SIDE_FG_SERVICE_COMPONENT));
    }
}
