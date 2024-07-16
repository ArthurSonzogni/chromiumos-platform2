// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/cellular/power_opt.h"

#include <base/containers/contains.h>
#include <base/time/time.h>
#include <chromeos/dbus/service_constants.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

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
  const base::TimeDelta DurationOverLastOnlineTreshold =
      PowerOpt::kLastOnlineLongThreshold + base::Seconds(10);
  const base::TimeDelta DurationOverLastManualConnectTrehsold =
      PowerOpt::kLastUserRequestThreshold + base::Seconds(10);
  const base::TimeDelta DurationBelowLastManualConnectTrehsold =
      PowerOpt::kLastUserRequestThreshold - base::Seconds(10);

  obj.AddOptInfoForNewService("123");
  obj.AddOptInfoForNewService("456");
  obj.UpdatePowerState("123", PowerOpt::PowerState::kOn);
  EXPECT_EQ(obj.GetPowerState("123"), PowerOpt::PowerState::kOn);
  EXPECT_FALSE(obj.UpdatePowerState("789", PowerOpt::PowerState::kOn));

  obj.UpdateManualConnectTime(base::Time::Now() -
                              DurationOverLastManualConnectTrehsold);
  obj.UpdateDurationSinceLastOnline(base::Time::Now() -
                                    DurationOverLastOnlineTreshold);
  // last online threshold met, and last manual connection threshold met. Low
  // power
  EXPECT_EQ(obj.GetPowerState("123"), PowerOpt::PowerState::kLow);

  obj.UpdatePowerState("123", PowerOpt::PowerState::kOn);
  obj.UpdateManualConnectTime(base::Time::Now() -
                              DurationBelowLastManualConnectTrehsold);
  obj.UpdateDurationSinceLastOnline(base::Time::Now() -
                                    DurationOverLastOnlineTreshold);
  // last online threshold met, but last manual connection threshold not met.
  // Stay power on.
  EXPECT_EQ(obj.GetPowerState("123"), PowerOpt::PowerState::kOn);
}

TEST_F(PowerOptTest, LowPowerInvalidApn) {
  PowerOpt obj(&manager_);
  const base::TimeDelta DurationOverLastOnlineTreshold =
      PowerOpt::kLastOnlineShortThreshold + base::Seconds(10);
  const base::TimeDelta DurationInvalidApn =
      PowerOpt::kNoServiceInvalidApnTimeThreshold + base::Seconds(100);
  obj.AddOptInfoForNewService("123");
  obj.UpdatePowerState("123", PowerOpt::PowerState::kOn);
  // set and met not online duration
  obj.UpdateDurationSinceLastOnline(base::Time::Now() -
                                    DurationOverLastOnlineTreshold);
  obj.current_opt_info_->last_connect_fail_invalid_apn_time =
      base::Time::Now() - DurationInvalidApn;
  EXPECT_EQ(obj.GetPowerState("123"), PowerOpt::PowerState::kOn);
  obj.NotifyConnectionFailInvalidApn("123");
  // met invalid apn duration, set to low power state
  EXPECT_EQ(obj.GetPowerState("123"), PowerOpt::PowerState::kLow);
}

}  // namespace shill
