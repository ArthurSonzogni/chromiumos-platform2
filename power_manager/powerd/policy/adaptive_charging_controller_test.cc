// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/common/fake_prefs.h"
#include "power_manager/powerd/policy/adaptive_charging_controller.h"
#include "power_manager/powerd/policy/backlight_controller_stub.h"
#include "power_manager/powerd/system/dbus_wrapper_stub.h"
#include "power_manager/powerd/system/input_watcher_stub.h"
#include "power_manager/powerd/system/power_supply_stub.h"

#include <string>
#include <utility>
#include <vector>

#include <base/containers/contains.h>
#include <base/files/file_enumerator.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/json/json_file_value_serializer.h>
#include <base/json/values_util.h>
#include <base/run_loop.h>
#include <dbus/bus.h>
#include <dbus/message.h>
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
      double display_battery_percentage) override {
    adaptive_state = state;
  }

  AdaptiveChargingController* adaptive_charging_controller_;
  // The vector of doubles that represent the probability of unplug for each
  // associated hour, except for the last result, which is the probability of
  // unplug after the corresponding hour for the second to last result.
  std::vector<double> fake_result;
  int fake_lower;
  int fake_upper;
  metrics::AdaptiveChargingState adaptive_state;
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
    power_status_.battery_state = PowerSupplyProperties_BatteryState_CHARGING;
    power_supply_.set_status(power_status_);
    adaptive_charging_controller_.set_recheck_alarm_for_testing(
        std::move(recheck_alarm));
    adaptive_charging_controller_.set_charge_alarm_for_testing(
        std::move(charge_alarm));
    prefs_.SetInt64(kAdaptiveChargingHoldPercentPref, kDefaultTestPercent);
    EXPECT_TRUE(temp_dir_.CreateUniqueTempDir());
    EXPECT_TRUE(temp_dir_.IsValid());
    charge_history_dir_ = temp_dir_.GetPath().Append("charge_history");
    charge_events_dir_ = charge_history_dir_.Append("charge_events");
    time_full_on_ac_dir_ = charge_history_dir_.Append("time_full_on_ac");
    time_on_ac_dir_ = charge_history_dir_.Append("time_on_ac");
    charge_history_ =
        adaptive_charging_controller_.get_charge_history_for_testing();
    charge_history_->set_charge_history_dir_for_testing(charge_history_dir_);
  }

  ~AdaptiveChargingControllerTest() override = default;

  void CreateDefaultChargeHistory() {
    CreateChargeHistoryDirectories();
    base::Time now = base::Time::Now();
    base::Time today = now.UTCMidnight();
    for (int i = 0; i < 15; ++i) {
      WriteChargeHistoryFile(time_on_ac_dir_, today - i * base::Days(1),
                             base::Hours(5));
      WriteChargeHistoryFile(time_full_on_ac_dir_, today - i * base::Days(1),
                             base::Hours(3));
    }
  }

  void InitNoHistory() {
    adaptive_charging_controller_.Init(&delegate_, &backlight_controller_,
                                       &input_watcher_, &power_supply_,
                                       &dbus_wrapper_, &prefs_);
    power_supply_.NotifyObservers();

    // Adaptive Charging is not enabled yet.
    EXPECT_EQ(delegate_.fake_lower, kBatterySustainDisabled);
    EXPECT_EQ(delegate_.fake_upper, kBatterySustainDisabled);

    PowerManagementPolicy policy;
    policy.set_adaptive_charging_enabled(true);
    adaptive_charging_controller_.HandlePolicyChange(policy);
  }

  void Init() {
    CreateDefaultChargeHistory();
    InitNoHistory();
    EXPECT_TRUE(charge_alarm_->IsRunning());
    EXPECT_TRUE(recheck_alarm_->IsRunning());
    EXPECT_EQ(delegate_.fake_lower, kDefaultTestPercent);
    EXPECT_EQ(delegate_.fake_upper, kDefaultTestPercent);
  }

  void InitFullChargeNoHistory() {
    power_status_.battery_percentage = 100;
    power_status_.display_battery_percentage = 100;
    power_status_.battery_state = PowerSupplyProperties_BatteryState_FULL;
    power_supply_.set_status(power_status_);
    adaptive_charging_controller_.Init(&delegate_, &backlight_controller_,
                                       &input_watcher_, &power_supply_,
                                       &dbus_wrapper_, &prefs_);
    power_supply_.NotifyObservers();

    // Adaptive Charging is not enabled yet.
    EXPECT_EQ(delegate_.fake_lower, kBatterySustainDisabled);
    EXPECT_EQ(delegate_.fake_upper, kBatterySustainDisabled);

    PowerManagementPolicy policy;
    policy.set_adaptive_charging_enabled(true);
    adaptive_charging_controller_.HandlePolicyChange(policy);

    // Adaptive Charging is not started when charge is full.
    EXPECT_EQ(delegate_.fake_lower, kBatterySustainDisabled);
    EXPECT_EQ(delegate_.fake_upper, kBatterySustainDisabled);
  }

  void InitFullCharge() {
    CreateDefaultChargeHistory();
    InitFullChargeNoHistory();
  }

  void DisconnectCharger() {
    power_status_.external_power =
        PowerSupplyProperties_ExternalPower_DISCONNECTED;
    power_status_.battery_state =
        PowerSupplyProperties_BatteryState_DISCHARGING;
    power_supply_.set_status(power_status_);
    power_supply_.NotifyObservers();
  }

  void ConnectCharger() {
    // Leave whether to set `power_status_.battery_state` to FULL or CHARGING to
    // the caller.
    power_status_.external_power = PowerSupplyProperties_ExternalPower_AC;
    power_supply_.set_status(power_status_);
    power_supply_.NotifyObservers();
  }

  void CreateChargeHistoryDirectories() {
    EXPECT_FALSE(base::DirectoryExists(charge_history_dir_));
    EXPECT_TRUE(CreateDirectory(charge_history_dir_));
    EXPECT_TRUE(CreateDirectory(charge_events_dir_));
    EXPECT_TRUE(CreateDirectory(time_full_on_ac_dir_));
    EXPECT_TRUE(CreateDirectory(time_on_ac_dir_));
  }

  base::Time FloorTime(base::Time time) {
    base::TimeDelta conv =
        time.ToDeltaSinceWindowsEpoch().FloorToMultiple(base::Minutes(15));
    return base::Time::FromDeltaSinceWindowsEpoch(conv);
  }

  void CreateChargeHistoryFile(const base::FilePath& dir,
                               const base::Time& start) {
    base::Value val = base::TimeToValue(FloorTime(start));
    absl::optional<base::FilePath> opt_path = base::ValueToFilePath(val);
    base::File file(dir.Append(opt_path.value()),
                    base::File::FLAG_CREATE_ALWAYS | base::File::FLAG_READ |
                        base::File::FLAG_WRITE);
    EXPECT_TRUE(file.IsValid());
  }

  int NumChargeHistoryFiles(const base::FilePath& dir) {
    base::FileEnumerator file_enum(dir, false, base::FileEnumerator::FILES);
    int num_files = 0;
    for (base::FilePath path = file_enum.Next(); !path.empty();
         path = file_enum.Next()) {
      num_files++;
    }
    return num_files;
  }

  void WriteChargeHistoryFile(const base::FilePath& dir,
                              const base::Time& start,
                              const base::TimeDelta& duration) {
    base::Value val = base::TimeToValue(FloorTime(start));
    absl::optional<base::FilePath> opt_path = base::ValueToFilePath(val);
    JSONFileValueSerializer serializer(dir.Append(opt_path.value()));
    EXPECT_TRUE(serializer.Serialize(base::TimeDeltaToValue(duration)));
  }

  bool ChargeHistoryFileExists(const base::FilePath& dir,
                               const base::Time& start) {
    base::Value val = base::TimeToValue(FloorTime(start));
    absl::optional<base::FilePath> opt_path = base::ValueToFilePath(val);
    return base::PathExists(dir.Append(opt_path.value()));
  }

  base::TimeDelta ReadTimeDeltaFromFile(const base::FilePath& path) {
    JSONFileValueDeserializer deserializer(path);
    int error;
    std::string error_msg;
    auto val_ptr = deserializer.Deserialize(&error, &error_msg);
    return base::ValueToTimeDelta(*val_ptr).value();
  }

  base::TimeDelta ReadChargeHistoryFile(const base::FilePath& dir,
                                        const base::Time& start) {
    base::Value val = base::TimeToValue(FloorTime(start));
    absl::optional<base::FilePath> opt_path = base::ValueToFilePath(val);
    return ReadTimeDeltaFromFile(dir.Append(opt_path.value()));
  }

 protected:
  FakeDelegate delegate_;
  policy::BacklightControllerStub backlight_controller_;
  system::InputWatcherStub input_watcher_;
  system::PowerSupplyStub power_supply_;
  system::DBusWrapperStub dbus_wrapper_;
  FakePrefs prefs_;
  brillo::timers::SimpleAlarmTimer* recheck_alarm_;
  brillo::timers::SimpleAlarmTimer* charge_alarm_;
  system::PowerStatus power_status_;
  base::ScopedTempDir temp_dir_;
  base::FilePath charge_history_dir_;
  base::FilePath charge_events_dir_;
  base::FilePath time_full_on_ac_dir_;
  base::FilePath time_on_ac_dir_;
  AdaptiveChargingController adaptive_charging_controller_;
  ChargeHistory* charge_history_;
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
  DisconnectCharger();
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

// Test that calling the ChargeNowForAdaptiveCharging method via dbus
// successfully stops Adaptive Charging.
TEST_F(AdaptiveChargingControllerTest, TestChargeNow) {
  Init();

  // Call the ChargeNow DBus method, then check that Adaptive Charging is
  // disabled.
  dbus::MethodCall method_call(kPowerManagerInterface,
                               kChargeNowForAdaptiveChargingMethod);
  std::unique_ptr<dbus::Response> response =
      dbus_wrapper_.CallExportedMethodSync(&method_call);
  EXPECT_TRUE(response &&
              response->GetMessageType() != dbus::Message::MESSAGE_ERROR);
  EXPECT_FALSE(recheck_alarm_->IsRunning());
  EXPECT_FALSE(charge_alarm_->IsRunning());
  EXPECT_EQ(delegate_.fake_lower, kBatterySustainDisabled);
  EXPECT_EQ(delegate_.fake_upper, kBatterySustainDisabled);

  // Check that Adaptive Charging successfully starts again after unplugging
  // then plugging the AC charger.
  DisconnectCharger();
  power_status_.battery_state = PowerSupplyProperties_BatteryState_CHARGING;
  ConnectCharger();
  EXPECT_TRUE(charge_alarm_->IsRunning());
  EXPECT_TRUE(recheck_alarm_->IsRunning());
  EXPECT_EQ(delegate_.fake_lower, kDefaultTestPercent);
  EXPECT_EQ(delegate_.fake_upper, kDefaultTestPercent);
}

// Test that we don't start Adaptive Charging when the battery is already full.
TEST_F(AdaptiveChargingControllerTest, TestFullCharge) {
  // This verifies that Adaptive Charging doesn't start when enabled via policy.
  InitFullCharge();

  // Verify that Adaptive Charging doesn't start on unplug/plug as well.
  DisconnectCharger();
  power_status_.battery_state = PowerSupplyProperties_BatteryState_FULL;
  ConnectCharger();
  EXPECT_FALSE(recheck_alarm_->IsRunning());
  EXPECT_FALSE(charge_alarm_->IsRunning());
  EXPECT_EQ(delegate_.fake_lower, kBatterySustainDisabled);
  EXPECT_EQ(delegate_.fake_upper, kBatterySustainDisabled);
}

// Test that no Adaptive Charging alarm is running on a suspend attempt when the
// charger is disconnected.
TEST_F(AdaptiveChargingControllerTest, TestNoAlarmOnBattery) {
  Init();
  DisconnectCharger();
  adaptive_charging_controller_.PrepareForSuspendAttempt();

  EXPECT_FALSE(recheck_alarm_->IsRunning());
  EXPECT_FALSE(charge_alarm_->IsRunning());
  EXPECT_EQ(delegate_.fake_lower, kBatterySustainDisabled);
  EXPECT_EQ(delegate_.fake_upper, kBatterySustainDisabled);
}

// Test that sub-directories are created, permissions are modified, and initial
// files are created when the base Charge History directory doesn't even exist.
TEST_F(AdaptiveChargingControllerTest, TestEmptyChargeHistory) {
  // Init will cause power_supply_ to notify observers, which will init Charge
  // History.
  InitNoHistory();

  // Check that directories are created.
  EXPECT_TRUE(base::DirectoryExists(charge_history_dir_));
  EXPECT_TRUE(base::DirectoryExists(charge_events_dir_));
  EXPECT_TRUE(base::DirectoryExists(time_full_on_ac_dir_));
  EXPECT_TRUE(base::DirectoryExists(time_on_ac_dir_));

  // Verify permissions of directories are such that only powerd and root can
  // read/write charge history.
  int mode;
  EXPECT_TRUE(base::GetPosixFilePermissions(charge_history_dir_, &mode));
  EXPECT_EQ(0700, mode);
  EXPECT_TRUE(base::GetPosixFilePermissions(charge_events_dir_, &mode));
  EXPECT_EQ(0700, mode);
  EXPECT_TRUE(base::GetPosixFilePermissions(time_full_on_ac_dir_, &mode));
  EXPECT_EQ(0700, mode);
  EXPECT_TRUE(base::GetPosixFilePermissions(time_on_ac_dir_, &mode));
  EXPECT_EQ(0700, mode);

  // Check that there is one empty file in `charge_events_dir_`, which indicates
  // the charger was plugged in, and hasn't been unplugged yet.
  base::FileEnumerator events_dir(charge_events_dir_, false,
                                  base::FileEnumerator::FILES);
  for (base::FilePath path = events_dir.Next(); !path.empty();
       path = events_dir.Next()) {
    // charge event should not have a duration yet.
    EXPECT_EQ(0, events_dir.GetInfo().GetSize());
  }
  EXPECT_EQ(1, NumChargeHistoryFiles(charge_events_dir_));

  // Check that the current day is created for the `time_full_on_ac_dir_` and
  // `time_on_ac_dir_`.
  base::FileEnumerator full_on_ac_dir(time_full_on_ac_dir_, false,
                                      base::FileEnumerator::FILES);
  for (base::FilePath path = full_on_ac_dir.Next(); !path.empty();
       path = full_on_ac_dir.Next()) {
    std::string contents;
    EXPECT_EQ(base::TimeDelta(), ReadTimeDeltaFromFile(path));
  }
  EXPECT_EQ(1, NumChargeHistoryFiles(time_full_on_ac_dir_));

  base::FileEnumerator on_ac_dir(time_full_on_ac_dir_, false,
                                 base::FileEnumerator::FILES);
  for (base::FilePath path = on_ac_dir.Next(); !path.empty();
       path = on_ac_dir.Next()) {
    std::string contents;
    EXPECT_EQ(base::TimeDelta(), ReadTimeDeltaFromFile(path));
  }
  EXPECT_EQ(1, NumChargeHistoryFiles(time_on_ac_dir_));
}

// Verify that timestamps are 15 minute aligned for privacy reasons.
TEST_F(AdaptiveChargingControllerTest, TestTimeAlignment) {
  // Make an initial charge event about 40 minutes ago (not unplugged yet).
  base::Time event_time = FloorTime(base::Time::Now() - base::Minutes(40));
  CreateChargeHistoryDirectories();
  CreateChargeHistoryFile(charge_events_dir_, event_time);
  InitNoHistory();

  // Disconnect power, which should cause Charge History to be written.
  DisconnectCharger();

  base::TimeDelta duration =
      ReadChargeHistoryFile(charge_events_dir_, event_time);
  EXPECT_TRUE(base::Contains(
      std::vector<base::TimeDelta>({base::Minutes(30), base::Minutes(45)}),
      duration));

  // The time on AC can be 15 minutes, 30 minutes, or 45 minutes. Since the
  // start of charging can be up to 55 minutes ago (and this would be floored to
  // 45 minutes), the time on AC could be 45 minutes. Since the time charging
  // could be split between two days (say 11 minutes and 29 minutes), the total
  // time charging could be 15 minutes (since both values are floored).
  EXPECT_TRUE(base::Contains(
      std::vector<base::TimeDelta>(
          {base::Minutes(15), base::Minutes(30), base::Minutes(45)}),
      charge_history_->GetTimeOnAC()));

  // Battery was never full.
  EXPECT_EQ(base::TimeDelta(), charge_history_->GetTimeFullOnAC());
}

// Test that all of the file updates that need to happen on unplug occur.
TEST_F(AdaptiveChargingControllerTest, HistoryWrittenOnUnplug) {
  base::Time event_time = FloorTime(base::Time::Now() - base::Days(3));
  CreateChargeHistoryDirectories();
  CreateChargeHistoryFile(charge_events_dir_, event_time);
  InitNoHistory();
  DisconnectCharger();

  EXPECT_GE(base::Days(3) + base::Minutes(15),
            ReadChargeHistoryFile(charge_events_dir_, event_time));
  EXPECT_LE(base::Days(3) - base::Minutes(15),
            ReadChargeHistoryFile(charge_events_dir_, event_time));
}

// Test that we record pending time to `time_full_on_ac_dir_` when entering
// suspend and shutdown.
TEST_F(AdaptiveChargingControllerTest, TimeFullWrittenOnLowPowerStates) {
  base::Time now = base::Time::Now();
  CreateChargeHistoryDirectories();
  CreateChargeHistoryFile(charge_events_dir_, now - base::Minutes(30));
  InitFullChargeNoHistory();

  adaptive_charging_controller_.PrepareForSuspendAttempt();
  base::TimeDelta duration_full_on_ac_file_times = base::TimeDelta();
  base::FileEnumerator full_on_ac_dir(time_full_on_ac_dir_, false,
                                      base::FileEnumerator::FILES);
  for (base::FilePath path = full_on_ac_dir.Next(); !path.empty();
       path = full_on_ac_dir.Next())
    duration_full_on_ac_file_times += ReadTimeDeltaFromFile(path);

  // The time in the files should total to 15, 30, or 45 minutes, depending on
  // how things are floored, whether the initial charge event time is close to
  // 45 minutes, and whether the duration of charge spans two days.
  EXPECT_GE(base::Minutes(45), duration_full_on_ac_file_times);
  EXPECT_LE(base::Minutes(15), duration_full_on_ac_file_times);
}

// Test that our retention policy is properly enforced on Init.
TEST_F(AdaptiveChargingControllerTest, HistoryRetentionOnInit) {
  // The first two events should be kept, since we delete events that are 30+
  // days old from the time of unplug (not plug in).
  base::Time now = base::Time::Now();
  std::vector<base::Time> event_times = {
      now - base::Days(7), now - base::Days(31), now - base::Days(32)};
  std::vector<base::TimeDelta> event_durations = {base::Hours(1), base::Days(2),
                                                  base::Hours(10)};
  CreateChargeHistoryDirectories();
  for (int i = 0; i < event_times.size(); i++) {
    WriteChargeHistoryFile(charge_events_dir_, FloorTime(event_times[i]),
                           event_durations[i]);
    WriteChargeHistoryFile(time_full_on_ac_dir_, event_times[i].UTCMidnight(),
                           event_durations[i] - base::Hours(1));
    WriteChargeHistoryFile(time_on_ac_dir_, event_times[i].UTCMidnight(),
                           event_durations[i]);
  }

  // Add in some days with no charging.
  for (base::Time date = now.UTCMidnight(); date > now - base::Days(5);
       date -= base::Days(1)) {
    WriteChargeHistoryFile(time_full_on_ac_dir_, date, base::TimeDelta());
    WriteChargeHistoryFile(time_on_ac_dir_, date, base::TimeDelta());
  }

  InitNoHistory();
  EXPECT_EQ(event_durations[0],
            ReadChargeHistoryFile(charge_events_dir_, event_times[0]));
  EXPECT_EQ(event_durations[1],
            ReadChargeHistoryFile(charge_events_dir_, event_times[1]));
  EXPECT_FALSE(ChargeHistoryFileExists(charge_events_dir_, event_times[2]));

  // 2 of the existing files, and the empty charge event created on Init since
  // the charger is connected.
  EXPECT_EQ(3, NumChargeHistoryFiles(charge_events_dir_));
  EXPECT_TRUE(ChargeHistoryFileExists(time_full_on_ac_dir_,
                                      event_times[0].UTCMidnight()));
  EXPECT_FALSE(ChargeHistoryFileExists(time_full_on_ac_dir_,
                                       event_times[1].UTCMidnight()));
  EXPECT_FALSE(ChargeHistoryFileExists(time_full_on_ac_dir_,
                                       event_times[2].UTCMidnight()));
  EXPECT_TRUE(
      ChargeHistoryFileExists(time_on_ac_dir_, event_times[0].UTCMidnight()));
  EXPECT_FALSE(
      ChargeHistoryFileExists(time_on_ac_dir_, event_times[1].UTCMidnight()));
  EXPECT_FALSE(
      ChargeHistoryFileExists(time_on_ac_dir_, event_times[2].UTCMidnight()));
  for (base::Time date = now.UTCMidnight(); date > now - base::Days(5);
       date -= base::Days(1)) {
    EXPECT_TRUE(ChargeHistoryFileExists(time_full_on_ac_dir_, date));
    EXPECT_TRUE(ChargeHistoryFileExists(time_on_ac_dir_, date));
  }

  EXPECT_EQ(6, NumChargeHistoryFiles(time_full_on_ac_dir_));
  EXPECT_EQ(6, NumChargeHistoryFiles(time_on_ac_dir_));
}

// Test that we limit the number of charge events to 50 on Init and when a new
// charge event is created.
TEST_F(AdaptiveChargingControllerTest, MaxChargeEvents) {
  CreateChargeHistoryDirectories();
  base::Time file_time = base::Time::Now() - base::Days(5);
  for (int i = 0; i < 100; i++) {
    WriteChargeHistoryFile(charge_events_dir_,
                           file_time + i * base::Minutes(30),
                           base::Minutes(15));
  }

  EXPECT_EQ(100, NumChargeHistoryFiles(charge_events_dir_));
  InitNoHistory();
  EXPECT_EQ(50, NumChargeHistoryFiles(charge_events_dir_));

  // Check that the correct charge event files still exist.
  for (int i = 51; i < 100; i++) {
    EXPECT_TRUE(ChargeHistoryFileExists(charge_events_dir_,
                                        file_time + i * base::Minutes(30)));
  }

  // Check that there are still 50 charge events after the latest charge event
  // has a duration written to it.
  DisconnectCharger();

  EXPECT_EQ(50, NumChargeHistoryFiles(charge_events_dir_));
}

// Test that the DaysOfHistory function works correctly.
TEST_F(AdaptiveChargingControllerTest, TestDaysOfHistory) {
  CreateChargeHistoryDirectories();
  base::Time now = base::Time::Now();
  base::Time today = now.UTCMidnight();
  for (int i = 0; i < 15; ++i) {
    WriteChargeHistoryFile(time_on_ac_dir_, today - (i + 5) * base::Days(1),
                           base::Hours(5));
    WriteChargeHistoryFile(time_full_on_ac_dir_,
                           today - (i + 5) * base::Days(1), base::Hours(2));
  }

  InitNoHistory();
  // ChargeHistory should append additional days between the last "time_on_ac"
  // day and now.
  EXPECT_EQ(20, charge_history_->DaysOfHistory());
}

// Test that the GetTime... functions works correctly.
TEST_F(AdaptiveChargingControllerTest, TestGetTimeFunctions) {
  CreateChargeHistoryDirectories();
  base::Time now = base::Time::Now();
  base::Time today = now.UTCMidnight();
  for (int i = 0; i < 15; ++i) {
    WriteChargeHistoryFile(time_on_ac_dir_, today - (i + 5) * base::Days(1),
                           base::Hours(5));
    WriteChargeHistoryFile(time_full_on_ac_dir_,
                           today - (i + 5) * base::Days(1), base::Hours(2));
  }

  CreateChargeHistoryFile(charge_events_dir_, now - base::Hours(10));

  InitNoHistory();
  base::TimeDelta time_on_ac = 15 * base::Hours(5) + base::Hours(10);
  EXPECT_EQ(time_on_ac, charge_history_->GetTimeOnAC());
  EXPECT_EQ(15 * base::Hours(2), charge_history_->GetTimeFullOnAC());

  // Check that disconnecting power (and thus finalizing charge history numbers
  // based on the current charge event) doesn't change the GetTime... values.
  DisconnectCharger();

  // The 10 hours may be split across two days, which may turn it into 9:45.
  EXPECT_GE(time_on_ac + base::Minutes(15), charge_history_->GetTimeOnAC());
  EXPECT_LE(time_on_ac - base::Minutes(15), charge_history_->GetTimeOnAC());
  EXPECT_EQ(15 * base::Hours(2), charge_history_->GetTimeFullOnAC());
}

// Test that only a few charge history days will result in Adaptive Charging
// being disabled by its heuristic.
TEST_F(AdaptiveChargingControllerTest, TestHeuristicDisabledOnDays) {
  CreateChargeHistoryDirectories();
  base::Time now = base::Time::Now();
  base::Time today = now.UTCMidnight();
  for (int i = 0; i < 5; ++i) {
    WriteChargeHistoryFile(time_on_ac_dir_, today - i * base::Days(1),
                           base::Hours(5));
    WriteChargeHistoryFile(time_full_on_ac_dir_, today - i * base::Days(1),
                           base::Hours(3));
  }

  InitNoHistory();
  EXPECT_EQ(delegate_.fake_lower, kBatterySustainDisabled);
  EXPECT_EQ(delegate_.fake_upper, kBatterySustainDisabled);
  DisconnectCharger();
  EXPECT_EQ(metrics::AdaptiveChargingState::HEURISTIC_DISABLED,
            delegate_.adaptive_state);
}

// Test that a sufficient number of days (min 14) tracked in ChargeHistory with
// a too low TimeFullOnAC / TimeOnAC ratio still results in Adaptive Charging
// being disabled by its heuristic.
TEST_F(AdaptiveChargingControllerTest, TestHeuristicDisabledOnRatio) {
  CreateChargeHistoryDirectories();
  base::Time now = base::Time::Now();
  base::Time today = now.UTCMidnight();
  for (int i = 0; i < 15; ++i) {
    WriteChargeHistoryFile(time_on_ac_dir_, today - i * base::Days(1),
                           base::Hours(5));
    WriteChargeHistoryFile(time_full_on_ac_dir_, today - i * base::Days(1),
                           base::Hours(2));
  }

  InitNoHistory();
  EXPECT_EQ(delegate_.fake_lower, kBatterySustainDisabled);
  EXPECT_EQ(delegate_.fake_upper, kBatterySustainDisabled);
  DisconnectCharger();
  EXPECT_EQ(metrics::AdaptiveChargingState::HEURISTIC_DISABLED,
            delegate_.adaptive_state);
}

}  // namespace policy
}  // namespace power_manager
