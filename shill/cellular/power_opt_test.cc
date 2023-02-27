// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/cellular/power_opt.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <base/containers/contains.h>
#include <base/time/time.h>
#include <chromeos/dbus/service_constants.h>
#include "dbus/shill/dbus-constants.h"

#include "shill/manager.h"
#include "shill/mock_control.h"
#include "shill/mock_device_info.h"
#include "shill/mock_manager.h"
#include "shill/mock_metrics.h"
#include "shill/test_event_dispatcher.h"

using testing::NiceMock;
using testing::Test;

namespace shill {

class PowerOptTest : public Test {
 public:
  PowerOptTest() : manager_(&control_, &dispatcher_, &metrics_) {}

 protected:
  EventDispatcherForTest dispatcher_;
  NiceMock<MockControl> control_;
  MockMetrics metrics_;
  NiceMock<MockManager> manager_;
};

TEST_F(PowerOptTest, LowPowerLongNotOnline) {
  PowerOpt obj(&manager_);
  const base::TimeDelta DurationSinceLastOnline1 =
      PowerOpt::kLastOnlineLongThreshold - base::Seconds(10);
  const base::TimeDelta DurationSinceLastOnline2 =
      PowerOpt::kLastOnlineLongThreshold + base::Seconds(10);
  obj.AddOptInfoForNewService("123");
  obj.AddOptInfoForNewService("456");
  obj.UpdatePowerState("123", PowerOpt::PowerState::kOn);
  EXPECT_EQ(obj.GetPowerState("123"), PowerOpt::PowerState::kOn);
  EXPECT_FALSE(obj.UpdatePowerState("789", PowerOpt::PowerState::kOn));

  obj.UpdateDurationSinceLastOnline(
      base::Time::Now() - DurationSinceLastOnline1, false);
  // threshold not met, still in power on state
  EXPECT_EQ(obj.GetPowerState("123"), PowerOpt::PowerState::kOn);
  obj.UpdateDurationSinceLastOnline(
      base::Time::Now() - DurationSinceLastOnline2, false);
  // threshold met, set to low power state
  EXPECT_EQ(obj.GetPowerState("123"), PowerOpt::PowerState::kLow);
}

TEST_F(PowerOptTest, LowPowerInvalidApn) {
  PowerOpt obj(&manager_);
  const base::TimeDelta DurationSinceLastOnline =
      PowerOpt::kLastOnlineShortThreshold + base::Seconds(10);
  const base::TimeDelta DurationInvalidApn =
      PowerOpt::kNoServiceInvalidApnTimeThreshold + base::Seconds(100);
  obj.AddOptInfoForNewService("123");
  obj.UpdatePowerState("123", PowerOpt::PowerState::kOn);
  // set and met not online duration
  obj.UpdateDurationSinceLastOnline(base::Time::Now() - DurationSinceLastOnline,
                                    false);
  obj.current_opt_info_->last_connect_fail_invalid_apn_time =
      base::Time::Now() - DurationInvalidApn;
  EXPECT_EQ(obj.GetPowerState("123"), PowerOpt::PowerState::kOn);
  obj.NotifyConnectionFailInvalidApn("123");
  // met invalid apn duration, set to low power state
  EXPECT_EQ(obj.GetPowerState("123"), PowerOpt::PowerState::kLow);
}

}  // namespace shill
