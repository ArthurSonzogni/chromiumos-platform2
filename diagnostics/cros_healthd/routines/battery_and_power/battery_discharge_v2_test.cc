// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/battery_and_power/battery_discharge_v2.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <base/check.h>
#include <base/test/task_environment.h>
#include <base/test/test_future.h>
#include <base/time/time.h>
#include <gtest/gtest.h>

#include "diagnostics/cros_healthd/routines/base_routine_control.h"
#include "diagnostics/cros_healthd/routines/battery_and_power/battery_discharge_constants.h"
#include "diagnostics/cros_healthd/routines/routine_v2_test_utils.h"
#include "diagnostics/cros_healthd/system/fake_powerd_adapter.h"
#include "diagnostics/cros_healthd/system/mock_context.h"
#include "diagnostics/mojom/public/cros_healthd_routines.mojom.h"

namespace diagnostics {
namespace {

namespace mojom = ::ash::cros_healthd::mojom;

constexpr double kStartingChargePercent = 80;
constexpr double kEndingChargePercent = 55;
constexpr double kChangeChargePercent = 80 - 55;

// With this value for maximum_discharge_percent_allowed, the routine should
// pass.
constexpr uint8_t kPassingPercent = 50;
// With this value for maximum_discharge_percent_allowed, the routine should
// fail.
constexpr uint8_t kFailingPercent = 1;
// With this value for maximum_discharge_percent_allowed, the routine should
// raise exception.
constexpr uint8_t kErrorPercent = 101;

constexpr base::TimeDelta kFullDuration = base::Seconds(12);
constexpr base::TimeDelta kHalfDuration = kFullDuration / 2;

power_manager::PowerSupplyProperties GetPowerSupplyProperties() {
  power_manager::PowerSupplyProperties power_supply_proto;
  power_supply_proto.set_battery_percent(kStartingChargePercent);
  power_supply_proto.set_battery_state(
      power_manager::PowerSupplyProperties_BatteryState_DISCHARGING);
  return power_supply_proto;
}

mojom::RoutineInquiryReplyPtr CreateInquiryReply() {
  return mojom::RoutineInquiryReply::NewUnplugAcAdapter(
      mojom::UnplugAcAdapterReply::New());
}

mojom::RoutineStatePtr GetRoutineState(BaseRoutineControl& routine) {
  base::test::TestFuture<mojom::RoutineStatePtr> state_future;
  routine.GetState(state_future.GetCallback());
  return state_future.Take();
}

class BatteryDischargeRoutineV2Test : public testing::Test {
 public:
  base::expected<std::unique_ptr<BaseRoutineControl>, mojom::SupportStatusPtr>
  CreateRoutine(const base::TimeDelta& exec_duration,
                uint8_t maximum_discharge_percent_allowed) {
    auto arg = mojom::BatteryDischargeRoutineArgument::New(
        exec_duration, maximum_discharge_percent_allowed);
    return BatteryDischargeRoutineV2::Create(mock_context(), arg);
  }

  void FastForwardBy(base::TimeDelta time) {
    task_environment_.FastForwardBy(time);
  }

  MockContext* mock_context() { return &mock_context_; }

  FakePowerdAdapter* fake_powerd_adapter() {
    return mock_context_.fake_powerd_adapter();
  }

  MockContext mock_context_;
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  std::unique_ptr<BaseRoutineControl> routine_;
};

// Test that the routine returns unsupported for invalid
// `maximum_discharge_percent_allowed` parameter.
TEST_F(BatteryDischargeRoutineV2Test,
       UnsupportedForInvalidMaximumDischargePercentAllowedValue) {
  auto routine_create = CreateRoutine(kFullDuration, kErrorPercent);
  ASSERT_FALSE(routine_create.has_value());
  auto support_status = std::move(routine_create.error());

  ASSERT_TRUE(support_status->is_unsupported());
  EXPECT_EQ(
      support_status->get_unsupported(),
      mojom::Unsupported::New("Invalid maximum discharge percent allowed value",
                              /*reason=*/nullptr));
}

// Test that the routine returns unsupported for invalid `exec_duration`
// parameter.
TEST_F(BatteryDischargeRoutineV2Test, UnsupportedForInvalidExecDuration) {
  auto routine_create = CreateRoutine(base::Seconds(0), kPassingPercent);
  ASSERT_FALSE(routine_create.has_value());
  auto support_status = std::move(routine_create.error());

  ASSERT_TRUE(support_status->is_unsupported());
  EXPECT_EQ(
      support_status->get_unsupported(),
      mojom::Unsupported::New(
          "Exec duration should not be less than or equal to zero seconds",
          /*reason=*/nullptr));
}

// Test that the routine runs successfully with the correct state transitions.
TEST_F(BatteryDischargeRoutineV2Test, RoutineSuccess) {
  auto power_supply_proto = GetPowerSupplyProperties();
  fake_powerd_adapter()->SetPowerSupplyProperties(power_supply_proto);

  auto routine_create = CreateRoutine(kFullDuration, kPassingPercent);
  ASSERT_TRUE(routine_create.has_value());
  auto routine = std::move(routine_create.value());
  routine->SetOnExceptionCallback(UnexpectedRoutineExceptionCallback());

  auto result = GetRoutineState(*routine);
  EXPECT_EQ(result->percentage, 0);
  EXPECT_TRUE(result->state_union->is_initialized());

  routine->Start();
  result = GetRoutineState(*routine);
  EXPECT_EQ(result->percentage, 0);
  EXPECT_TRUE(result->state_union->is_waiting());

  routine->ReplyInquiry(CreateInquiryReply());
  result = GetRoutineState(*routine);
  EXPECT_EQ(result->percentage, 0);
  EXPECT_TRUE(result->state_union->is_running());

  power_supply_proto.set_battery_percent(kEndingChargePercent);
  fake_powerd_adapter()->SetPowerSupplyProperties(power_supply_proto);
  FastForwardBy(kFullDuration);

  result = GetRoutineState(*routine);
  EXPECT_EQ(result->percentage, 100);
  EXPECT_TRUE(result->state_union->is_finished());
  auto& finished_state = result->state_union->get_finished();
  EXPECT_TRUE(finished_state->has_passed);
  EXPECT_TRUE(finished_state->detail->is_battery_discharge());
  EXPECT_EQ(finished_state->detail->get_battery_discharge()->discharge_percent,
            kChangeChargePercent);
}

// Test that the routine returns failure if battery discharge exceed threshold.
TEST_F(BatteryDischargeRoutineV2Test, ExceedMaxDischargeRoutineFailure) {
  auto power_supply_proto = GetPowerSupplyProperties();
  fake_powerd_adapter()->SetPowerSupplyProperties(power_supply_proto);

  auto routine_create = CreateRoutine(kFullDuration, kFailingPercent);
  ASSERT_TRUE(routine_create.has_value());
  auto routine = std::move(routine_create.value());
  routine->SetOnExceptionCallback(UnexpectedRoutineExceptionCallback());
  routine->Start();
  routine->ReplyInquiry(CreateInquiryReply());

  power_supply_proto.set_battery_percent(kEndingChargePercent);
  fake_powerd_adapter()->SetPowerSupplyProperties(power_supply_proto);
  FastForwardBy(kFullDuration);

  auto result = GetRoutineState(*routine);
  EXPECT_EQ(result->percentage, 100);
  EXPECT_TRUE(result->state_union->is_finished());
  auto& finished_state = result->state_union->get_finished();
  EXPECT_FALSE(finished_state->has_passed);
  EXPECT_TRUE(finished_state->detail->is_battery_discharge());
  EXPECT_EQ(finished_state->detail->get_battery_discharge()->discharge_percent,
            kChangeChargePercent);
}

// Test that the routine raises exception if battery is not discharging.
TEST_F(BatteryDischargeRoutineV2Test, BatteryNotDischargingException) {
  auto power_supply_proto = GetPowerSupplyProperties();
  power_supply_proto.set_battery_state(
      power_manager::PowerSupplyProperties_BatteryState_CHARGING);
  fake_powerd_adapter()->SetPowerSupplyProperties(power_supply_proto);

  base::test::TestFuture<uint32_t, const std::string&> exception_future;
  auto routine_create = CreateRoutine(kFullDuration, kPassingPercent);
  ASSERT_TRUE(routine_create.has_value());
  auto routine = std::move(routine_create.value());
  routine->SetOnExceptionCallback(exception_future.GetCallback());
  routine->Start();
  routine->ReplyInquiry(CreateInquiryReply());

  FastForwardBy(kFullDuration);
  auto [error_unused, reason] = exception_future.Take();
  EXPECT_EQ(reason, kBatteryDischargeRoutineNotDischargingMessage);
}

// Test that the routine raises exception if battery ends with charger higher
// than it started.
TEST_F(BatteryDischargeRoutineV2Test,
       EndingChargeHigherThanStartingChargeException) {
  auto power_supply_proto = GetPowerSupplyProperties();
  power_supply_proto.set_battery_percent(kEndingChargePercent);
  fake_powerd_adapter()->SetPowerSupplyProperties(power_supply_proto);

  base::test::TestFuture<uint32_t, const std::string&> exception_future;
  auto routine_create = CreateRoutine(kFullDuration, kPassingPercent);
  ASSERT_TRUE(routine_create.has_value());
  auto routine = std::move(routine_create.value());
  routine->SetOnExceptionCallback(exception_future.GetCallback());
  routine->Start();
  routine->ReplyInquiry(CreateInquiryReply());

  power_supply_proto.set_battery_percent(kStartingChargePercent);
  fake_powerd_adapter()->SetPowerSupplyProperties(power_supply_proto);

  FastForwardBy(kFullDuration);
  auto [error_unused, reason] = exception_future.Take();
  EXPECT_EQ(reason, kBatteryDischargeRoutineNotDischargingMessage);
}

// Test that the routine raises exception with powerd error.
TEST_F(BatteryDischargeRoutineV2Test, PowerdErrorException) {
  fake_powerd_adapter()->SetPowerSupplyProperties(std::nullopt);

  base::test::TestFuture<uint32_t, const std::string&> exception_future;
  auto routine_create = CreateRoutine(kFullDuration, kPassingPercent);
  ASSERT_TRUE(routine_create.has_value());
  auto routine = std::move(routine_create.value());
  routine->SetOnExceptionCallback(exception_future.GetCallback());
  routine->Start();
  routine->ReplyInquiry(CreateInquiryReply());

  FastForwardBy(kFullDuration);
  auto [error_unused, reason] = exception_future.Take();
  EXPECT_EQ(reason, kPowerdPowerSupplyPropertiesFailedMessage);
}

// Test that the routine raises exception for powerd error after delaytask.
TEST_F(BatteryDischargeRoutineV2Test, DelayedTaskPowerdException) {
  auto power_supply_proto = GetPowerSupplyProperties();
  fake_powerd_adapter()->SetPowerSupplyProperties(power_supply_proto);

  base::test::TestFuture<uint32_t, const std::string&> exception_future;
  auto routine_create = CreateRoutine(kFullDuration, kPassingPercent);
  ASSERT_TRUE(routine_create.has_value());
  auto routine = std::move(routine_create.value());
  routine->SetOnExceptionCallback(exception_future.GetCallback());
  routine->Start();
  routine->ReplyInquiry(CreateInquiryReply());

  fake_powerd_adapter()->SetPowerSupplyProperties(std::nullopt);

  FastForwardBy(kFullDuration);
  auto [error_unused, reason] = exception_future.Take();
  EXPECT_EQ(reason, kPowerdPowerSupplyPropertiesFailedMessage);
}

// Test that the routine reports routine percentage correctly.
TEST_F(BatteryDischargeRoutineV2Test, RoutinePercentReportCorrect) {
  auto power_supply_proto = GetPowerSupplyProperties();
  fake_powerd_adapter()->SetPowerSupplyProperties(power_supply_proto);

  auto routine_create = CreateRoutine(kFullDuration, kPassingPercent);
  ASSERT_TRUE(routine_create.has_value());
  auto routine = std::move(routine_create.value());
  routine->SetOnExceptionCallback(UnexpectedRoutineExceptionCallback());
  routine->Start();
  routine->ReplyInquiry(CreateInquiryReply());

  auto result = GetRoutineState(*routine);
  EXPECT_EQ(result->percentage, 0);
  EXPECT_TRUE(result->state_union->is_running());

  FastForwardBy(kHalfDuration);
  result = GetRoutineState(*routine);
  EXPECT_EQ(result->percentage, 50);
  EXPECT_TRUE(result->state_union->is_running());
}

}  // namespace
}  // namespace diagnostics
