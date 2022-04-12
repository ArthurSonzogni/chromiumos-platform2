// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/common/fake_prefs.h"
#include "power_manager/powerd/policy/adaptive_charging_controller.h"
#include "power_manager/powerd/policy/backlight_controller_stub.h"
#include "power_manager/powerd/system/input_watcher_stub.h"
#include "power_manager/powerd/system/power_supply_stub.h"

#include <utility>
#include <vector>

#include <base/run_loop.h>
#include <gtest/gtest.h>

namespace power_manager {
namespace policy {

namespace {
const int64_t kBatterySustainDisabled = -1;
// Make this different from the default in adaptive_charging_controller.cc to
// make sure the interface works correctly with other values.
const int64_t kDefaultTestPercent = 70;

class FakeDelegate : public AdaptiveChargingControllerInterface::Delegate {
 public:
  bool SetBatterySustain(int lower, int upper) override {
    fake_lower = lower;
    fake_upper = upper;
    return true;
  }

  void GetAdaptiveChargingPrediction(const assist_ranker::RankerExample& proto,
                                     bool async) override {
    adaptive_charging_controller_->OnPredictionResponse(true, fake_result);
  }

  void GenerateAdaptiveChargingUnplugMetrics(
      const metrics::AdaptiveChargingState state,
      const base::TimeTicks& target_time,
      const base::TimeTicks& hold_start_time,
      const base::TimeTicks& hold_end_time,
      const base::TimeTicks& charge_finished_time,
      double display_battery_percentage) override {}

  AdaptiveChargingController* adaptive_charging_controller_;
  // The vector of doubles that represent the probability of unplug for each
  // associated hour, except for the last result, which is the probability of
  // unplug after the corresponding hour for the second to last result.
  std::vector<double> fake_result;
  int fake_lower;
  int fake_upper;
};

}  // namespace

class AdaptiveChargingControllerTest : public ::testing::Test {
 public:
  AdaptiveChargingControllerTest() {
    auto recheck_alarm = brillo::timers::SimpleAlarmTimer::CreateForTesting();
    auto charge_alarm = brillo::timers::SimpleAlarmTimer::CreateForTesting();
    recheck_alarm_ = recheck_alarm.get();
    charge_alarm_ = charge_alarm.get();
    delegate_.adaptive_charging_controller_ = &adaptive_charging_controller_;
    delegate_.fake_result = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0};
    delegate_.fake_lower = kBatterySustainDisabled;
    delegate_.fake_upper = kBatterySustainDisabled;
    power_status_.external_power = PowerSupplyProperties_ExternalPower_AC;
    power_status_.display_battery_percentage = kDefaultTestPercent;
    power_supply_.set_status(power_status_);
    adaptive_charging_controller_.set_recheck_alarm_for_testing(
        std::move(recheck_alarm));
    adaptive_charging_controller_.set_charge_alarm_for_testing(
        std::move(charge_alarm));
    prefs_.SetInt64(kAdaptiveChargingHoldPercentPref, kDefaultTestPercent);
  }

  ~AdaptiveChargingControllerTest() override = default;

  void Init() {
    adaptive_charging_controller_.Init(&delegate_, &backlight_controller_,
                                       &input_watcher_, &power_supply_,
                                       &prefs_);
    power_supply_.NotifyObservers();

    // Adaptive Charging is not enabled yet.
    EXPECT_EQ(delegate_.fake_lower, kBatterySustainDisabled);
    EXPECT_EQ(delegate_.fake_upper, kBatterySustainDisabled);

    PowerManagementPolicy policy;
    policy.set_adaptive_charging_enabled(true);
    adaptive_charging_controller_.HandlePolicyChange(policy);

    EXPECT_TRUE(charge_alarm_->IsRunning());
    EXPECT_TRUE(recheck_alarm_->IsRunning());
    EXPECT_EQ(delegate_.fake_lower, kDefaultTestPercent);
    EXPECT_EQ(delegate_.fake_upper, kDefaultTestPercent);
  }

 protected:
  FakeDelegate delegate_;
  policy::BacklightControllerStub backlight_controller_;
  system::InputWatcherStub input_watcher_;
  system::PowerSupplyStub power_supply_;
  FakePrefs prefs_;
  brillo::timers::SimpleAlarmTimer* recheck_alarm_;
  brillo::timers::SimpleAlarmTimer* charge_alarm_;
  system::PowerStatus power_status_;
  AdaptiveChargingController adaptive_charging_controller_;
};

// Test that the alarms are properly set when Adaptive Charging starts, when the
// power_status is updated, and when suspend occurs.
TEST_F(AdaptiveChargingControllerTest, TestAlarmSet) {
  // Set the display_battery_percentage to be less than the hold percent, so
  // that the target full charge time can increase.
  power_status_.display_battery_percentage = kDefaultTestPercent - 10.0;
  power_supply_.set_status(power_status_);
  delegate_.fake_result = {0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 0.0};
  Init();

  // Record the initial charge delay with the `fake_result` as defined above.
  base::TimeDelta start_delta =
      adaptive_charging_controller_.get_charge_delay_for_testing();

  // This will trigger the `AdaptiveChargingController::recheck_alarm_`, which
  // will set a new charge delay.
  delegate_.fake_result[3] = 0.0;
  delegate_.fake_result[5] = 1.0;
  adaptive_charging_controller_.set_recheck_delay_for_testing(
      base::TimeDelta());
  base::RunLoop().RunUntilIdle();
  base::TimeDelta recheck_delta =
      adaptive_charging_controller_.get_charge_delay_for_testing();

  // We extended the prediction for when the system would unplug by two hours,
  // but just check for > 1 hour due to timestamps being slightly off.
  EXPECT_GT(recheck_delta - start_delta, base::Hours(1));

  // This will set yet another charge delay, as triggered by a suspend attempt.
  delegate_.fake_result[5] = 0.0;
  delegate_.fake_result[7] = 1.0;
  adaptive_charging_controller_.PrepareForSuspendAttempt();
  base::TimeDelta suspend_delta =
      adaptive_charging_controller_.get_charge_delay_for_testing();
  EXPECT_GT(suspend_delta - recheck_delta, base::Hours(1));
}

// Test that the command to the EC to clear the battery sustain status is sent
// when AdaptiveChargingController should disable it.
TEST_F(AdaptiveChargingControllerTest, TestBatterySustainClearedDisconnect) {
  Init();
  // When external power is unplugged.
  power_status_.external_power =
      PowerSupplyProperties_ExternalPower_DISCONNECTED;
  power_supply_.set_status(power_status_);
  power_supply_.NotifyObservers();
  EXPECT_FALSE(recheck_alarm_->IsRunning());
  EXPECT_FALSE(charge_alarm_->IsRunning());
  EXPECT_EQ(delegate_.fake_lower, kBatterySustainDisabled);
  EXPECT_EQ(delegate_.fake_upper, kBatterySustainDisabled);
}

// Test that a change in prediction to the system unplugged soon will result in
// Adaptive Charging being stopped.
TEST_F(AdaptiveChargingControllerTest, TestNoDelayOnPrediction) {
  Init();
  delegate_.fake_result = {1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
  adaptive_charging_controller_.set_recheck_delay_for_testing(
      base::TimeDelta());
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(recheck_alarm_->IsRunning());
  EXPECT_FALSE(charge_alarm_->IsRunning());
  EXPECT_EQ(delegate_.fake_lower, kBatterySustainDisabled);
  EXPECT_EQ(delegate_.fake_upper, kBatterySustainDisabled);
}

// Test that disabling Adaptive Charging via DBus works correctly.
TEST_F(AdaptiveChargingControllerTest, TestDBusEnableDisable) {
  PowerManagementPolicy policy;
  Init();
  policy.set_adaptive_charging_enabled(false);
  adaptive_charging_controller_.HandlePolicyChange(policy);

  // We still run the recheck and charge alarm to report metrics.
  EXPECT_TRUE(recheck_alarm_->IsRunning());
  EXPECT_TRUE(charge_alarm_->IsRunning());
  EXPECT_EQ(delegate_.fake_lower, kBatterySustainDisabled);
  EXPECT_EQ(delegate_.fake_upper, kBatterySustainDisabled);
}

// Test that the charge alarm will enable charging when it goes off.
TEST_F(AdaptiveChargingControllerTest, TestChargeAlarm) {
  Init();
  adaptive_charging_controller_.set_charge_delay_for_testing(base::TimeDelta());
  base::RunLoop().RunUntilIdle();

  EXPECT_FALSE(recheck_alarm_->IsRunning());
  EXPECT_FALSE(charge_alarm_->IsRunning());
  EXPECT_EQ(delegate_.fake_lower, kBatterySustainDisabled);
  EXPECT_EQ(delegate_.fake_upper, kBatterySustainDisabled);
}

TEST_F(AdaptiveChargingControllerTest, TestStoppedOnShutdown) {
  Init();
  adaptive_charging_controller_.HandleShutdown();

  EXPECT_FALSE(recheck_alarm_->IsRunning());
  EXPECT_FALSE(charge_alarm_->IsRunning());
  EXPECT_EQ(delegate_.fake_lower, kBatterySustainDisabled);
  EXPECT_EQ(delegate_.fake_upper, kBatterySustainDisabled);
}

// Test that the sustain settings are set based on
// `PowerStatus.display_battery_percentage` when it's higher than
// `hold_percent_`.
TEST_F(AdaptiveChargingControllerTest, TestAdjustedSustain) {
  Init();

  PowerManagementPolicy policy;
  policy.set_adaptive_charging_hold_percent(kDefaultTestPercent - 10);
  adaptive_charging_controller_.HandlePolicyChange(policy);

  EXPECT_TRUE(charge_alarm_->IsRunning());
  EXPECT_TRUE(recheck_alarm_->IsRunning());
  EXPECT_EQ(delegate_.fake_lower, kDefaultTestPercent);
  EXPECT_EQ(delegate_.fake_upper, kDefaultTestPercent);
}

// Test that we set an infinite charge delay when the charger is expected to be
// unplugged > 8 hours from now.
TEST_F(AdaptiveChargingControllerTest, TestMaxTimeSustain) {
  Init();

  delegate_.fake_result = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0};
  // Disable then enable Adaptive Charging to allow the charge delay to go up,
  // since we will already have a target charge time from calling Init and we
  // reached the hold percent (which prevents the charge delay from increasing).
  PowerManagementPolicy policy;
  policy.set_adaptive_charging_enabled(false);
  adaptive_charging_controller_.HandlePolicyChange(policy);
  policy.set_adaptive_charging_enabled(true);
  adaptive_charging_controller_.HandlePolicyChange(policy);

  // The TimeTicks value is a max int, not an infinite value.
  EXPECT_FALSE(charge_alarm_->IsRunning());
  EXPECT_EQ(
      base::TimeTicks::Max(),
      adaptive_charging_controller_.get_target_full_charge_time_for_testing());
  EXPECT_EQ(delegate_.fake_lower, kDefaultTestPercent);
  EXPECT_EQ(delegate_.fake_upper, kDefaultTestPercent);
}

// Test that we stop delaying charge if there's no probability above
// `min_probability_`.
TEST_F(AdaptiveChargingControllerTest, TestResultLessThanMinProbability) {
  prefs_.SetDouble(kAdaptiveChargingMinProbabilityPref, 0.5);
  Init();

  // Set a slightly higher fake result for an hour that would still result in
  // delaying charging if it were selected for the prediction.
  delegate_.fake_result = std::vector<double>(9, 0.1);
  delegate_.fake_result[5] = 0.2;
  adaptive_charging_controller_.set_recheck_delay_for_testing(
      base::TimeDelta());
  base::RunLoop().RunUntilIdle();

  // Adaptive Charging should be stopped.
  EXPECT_FALSE(recheck_alarm_->IsRunning());
  EXPECT_FALSE(charge_alarm_->IsRunning());
  EXPECT_EQ(delegate_.fake_lower, kBatterySustainDisabled);
  EXPECT_EQ(delegate_.fake_upper, kBatterySustainDisabled);
}

}  // namespace policy
}  // namespace power_manager
