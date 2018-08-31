// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <utility>

#include <base/strings/stringprintf.h>
#include <gtest/gtest.h>

#include "power_manager/common/action_recorder.h"
#include "power_manager/common/power_constants.h"
#include "power_manager/powerd/policy/sar_handler.h"
#include "power_manager/powerd/system/sar_observer.h"
#include "power_manager/powerd/system/sar_watcher_stub.h"

namespace power_manager {
namespace policy {

namespace {

constexpr char kWifiSensorDetected[] = "WifiDelegate::ProximitySensorDetected";
constexpr char kLteSensorDetected[] = "LteDelegate::ProximitySensorDetected";

constexpr char kWifiChangeNear[] = "WifiDelegate::HandleProximityChange(near)";
constexpr char kWifiChangeFar[] = "WifiDelegate::HandleProximityChange(far)";

class WifiDelegate : public SarHandler::Delegate, public ActionRecorder {
 public:
  WifiDelegate() = default;
  ~WifiDelegate() override = default;

  // SarHandler::Delegate overrides:
  void ProximitySensorDetected(UserProximity value) override {
    AppendAction(kWifiSensorDetected);
  }
  void HandleProximityChange(UserProximity value) override {
    auto action = base::StringPrintf("WifiDelegate::HandleProximityChange(%s)",
                                     UserProximityToString(value).c_str());
    AppendAction(action);
  }
};

class LteDelegate : public SarHandler::Delegate, public ActionRecorder {
 public:
  LteDelegate() = default;
  ~LteDelegate() override = default;

  // SarHandler::Delegate overrides:
  void ProximitySensorDetected(UserProximity value) override {
    AppendAction(kLteSensorDetected);
  }
  void HandleProximityChange(UserProximity value) override {
    auto action = base::StringPrintf("LteDelegate::HandleProximityChange(%s)",
                                     UserProximityToString(value).c_str());
    AppendAction(action);
  }
};

class SarHandlerTest : public ::testing::Test {
 public:
  SarHandlerTest() {
    sar_handler_.Init(&sar_watcher_, &wifi_delegate_, &lte_delegate_);
  }

 protected:
  system::SarWatcherStub sar_watcher_;
  WifiDelegate wifi_delegate_;
  LteDelegate lte_delegate_;
  SarHandler sar_handler_;
};

}  // namespace

TEST_F(SarHandlerTest, DetectSensor) {
  sar_watcher_.AddSensor(1, system::SarWatcher::SensorRole::SENSOR_ROLE_WIFI);
  CHECK_EQ(JoinActions(kWifiSensorDetected, nullptr),
           wifi_delegate_.GetActions());

  sar_watcher_.AddSensor(1, system::SarWatcher::SensorRole::SENSOR_ROLE_LTE);
  CHECK_EQ(JoinActions(kLteSensorDetected, nullptr),
           lte_delegate_.GetActions());
}

TEST_F(SarHandlerTest, ProximityChange) {
  sar_watcher_.AddSensor(1, system::SarWatcher::SensorRole::SENSOR_ROLE_WIFI);
  sar_watcher_.AddSensor(2, system::SarWatcher::SensorRole::SENSOR_ROLE_WIFI);
  wifi_delegate_.GetActions();  //  consume the detection events

  sar_watcher_.SendEvent(1, UserProximity::FAR);
  CHECK_EQ(JoinActions(NULL), wifi_delegate_.GetActions());

  sar_watcher_.SendEvent(2, UserProximity::FAR);
  CHECK_EQ(JoinActions(kWifiChangeFar, nullptr), wifi_delegate_.GetActions());

  sar_watcher_.SendEvent(1, UserProximity::NEAR);
  CHECK_EQ(JoinActions(kWifiChangeNear, nullptr), wifi_delegate_.GetActions());
  sar_watcher_.SendEvent(2, UserProximity::NEAR);
  CHECK_EQ(JoinActions(NULL), wifi_delegate_.GetActions());
}

}  // namespace policy
}  // namespace power_manager
