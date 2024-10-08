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

#define STATSD_DEBUG false  // STOPSHIP if true
#include "Log.h"

#include "statscompanion_util.h"
#include <android/binder_auto_utils.h>
#include <android/binder_manager.h>

namespace android {
namespace os {
namespace statsd {

shared_ptr<IStatsCompanionService> getStatsCompanionService(const bool blocking) {
    ::ndk::SpAIBinder binder;
    if (blocking) {
        binder = ndk::SpAIBinder(AServiceManager_getService("statscompanion"));
    } else {
        binder = ndk::SpAIBinder(AServiceManager_checkService("statscompanion"));
    }
    return IStatsCompanionService::fromBinder(binder);
}

}  // namespace statsd
}  // namespace os
}  // namespace android
