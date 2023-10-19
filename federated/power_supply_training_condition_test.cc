// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <unordered_map>
#include <utility>

#include <base/check.h>
#include <base/functional/callback.h>
#include <base/memory/ref_counted.h>
#include <dbus/message.h>
#include <dbus/mock_bus.h>
#include <dbus/mock_object_proxy.h>
#include <dbus/power_manager/dbus-constants.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "federated/power_supply_training_condition.h"
#include "power_manager/proto_bindings/battery_saver.pb.h"
#include "power_manager/proto_bindings/power_supply_properties.pb.h"

namespace federated {
namespace {

using ::power_manager::kBatterySaverModeStateChanged;
using ::power_manager::kGetBatterySaverModeState;
using ::power_manager::kPowerManagerInterface;
using ::power_manager::kPowerManagerServiceName;
using ::power_manager::kPowerManagerServicePath;
using ::power_manager::kPowerSupplyPollSignal;
using ::testing::_;
using ::testing::Invoke;
using ::testing::SaveArg;
using ::testing::StrictMock;

// Generates a PowerSupplyProperties proto with the given battery percent and
// state.
power_manager::PowerSupplyProperties GeneratePowerSupplyProto(
    const double battery_percent,
    power_manager::PowerSupplyProperties::BatteryState battery_state) {
  power_manager::PowerSupplyProperties power_supply_proto;
  power_supply_proto.set_battery_percent(battery_percent);
  power_supply_proto.set_battery_state(battery_state);

  return power_supply_proto;
}

}  // namespace

class PowerSupplyTrainingConditionTest : public ::testing::Test {
 public:
  PowerSupplyTrainingConditionTest()
      : mock_dbus_(new StrictMock<dbus::MockBus>(dbus::Bus::Options())),
        dbus_object_proxy_(new StrictMock<dbus::MockObjectProxy>(
            mock_dbus_.get(),
            kPowerManagerServiceName,
            dbus::ObjectPath(kPowerManagerServicePath))) {}
  PowerSupplyTrainingConditionTest(const PowerSupplyTrainingConditionTest&) =
      delete;
  PowerSupplyTrainingConditionTest& operator=(
      const PowerSupplyTrainingConditionTest&) = delete;

  void SetUp() override {
    EXPECT_CALL(*mock_dbus_,
                GetObjectProxy(kPowerManagerServiceName,
                               dbus::ObjectPath(kPowerManagerServicePath)))
        .WillOnce(Return(dbus_object_proxy_.get()));

    EXPECT_CALL(
        *dbus_object_proxy_,
        DoConnectToSignal(kPowerManagerInterface, kPowerSupplyPollSignal, _, _))
        .WillOnce(SaveArg<2>(&on_signal_callbacks_[kPowerSupplyPollSignal]));

    EXPECT_CALL(*dbus_object_proxy_,
                DoConnectToSignal(kPowerManagerInterface,
                                  kBatterySaverModeStateChanged, _, _))
        .WillOnce(
            SaveArg<2>(&on_signal_callbacks_[kBatterySaverModeStateChanged]));

    EXPECT_CALL(*dbus_object_proxy_, DoWaitForServiceToBeAvailable(_))
        .WillOnce(Invoke(
            [](dbus::MockObjectProxy::WaitForServiceToBeAvailableCallback*
                   callback) { std::move(*callback).Run(true); }));

    EXPECT_CALL(*dbus_object_proxy_, CallMethodAndBlock(_, _))
        .WillOnce(Invoke(
            this,
            &PowerSupplyTrainingConditionTest::RespondGetBatterySaverMode));

    power_supply_training_condition_ =
        std::make_unique<PowerSupplyTrainingCondition>(mock_dbus_.get());
  }

  // Creates a dbus signal with the given `signal_name`, writes the given string
  // (a serialized proto or "") to it and invokes.
  void CreateSignalAndInvoke(const std::string& serialized_proto,
                             const std::string& signal_name) {
    dbus::Signal signal(kPowerManagerInterface, signal_name);
    dbus::MessageWriter writer(&signal);
    writer.AppendArrayOfBytes(
        reinterpret_cast<const uint8_t*>(serialized_proto.data()),
        serialized_proto.size());

    auto callback_iter = on_signal_callbacks_.find(signal_name);
    ASSERT_NE(callback_iter, on_signal_callbacks_.end());
    callback_iter->second.Run(&signal);
  }

  // Initialises the battery saver mode as disabled.
  std::unique_ptr<dbus::Response> RespondGetBatterySaverMode(
      dbus::MethodCall* method_call, int timeout_msec) {
    EXPECT_EQ(method_call->GetInterface(), kPowerManagerInterface);
    EXPECT_EQ(method_call->GetMember(), kGetBatterySaverModeState);
    method_call->SetSerial(1);  // Needs to be non-zero or it fails.
    std::unique_ptr<dbus::Response> response =
        dbus::Response::FromMethodCall(method_call);
    dbus::MessageWriter writer(response.get());
    power_manager::BatterySaverModeState battery_saver_mode_state;
    battery_saver_mode_state.set_enabled(false);
    writer.AppendProtoAsArrayOfBytes(battery_saver_mode_state);
    return response;
  }

  PowerSupplyTrainingCondition* power_supply_training_condition() const {
    DCHECK(power_supply_training_condition_);
    return power_supply_training_condition_.get();
  }

 private:
  scoped_refptr<StrictMock<dbus::MockBus>> mock_dbus_;

  scoped_refptr<StrictMock<dbus::MockObjectProxy>> dbus_object_proxy_;

  std::unique_ptr<PowerSupplyTrainingCondition>
      power_supply_training_condition_;

  std::unordered_map<std::string,
                     base::RepeatingCallback<void(dbus::Signal* signal)>>
      on_signal_callbacks_;
};

// Tests PowerSupply is initialized correctly and can handle unexpected
// empty proto messages.
TEST_F(PowerSupplyTrainingConditionTest, EmptyPowerSupplyPollSignal) {
  // Initialized as false;
  EXPECT_FALSE(
      power_supply_training_condition()->IsTrainingConditionSatisfied());

  // Invoke kPowerSupplyPollSignal with an empty proto message.
  CreateSignalAndInvoke("", kPowerSupplyPollSignal);
  EXPECT_FALSE(
      power_supply_training_condition()->IsTrainingConditionSatisfied());
}

TEST_F(PowerSupplyTrainingConditionTest, PowerSupplySatisfied) {
  // Note: here we must create new signals rather than write to the old one,
  // because the WriteSerializedProtoToSignal is effectively "append", only the
  // first written string can be read by the DeviceStatusMonitor.

  // battery = 95, state = discharging, satisfied.
  CreateSignalAndInvoke(
      GeneratePowerSupplyProto(
          95.0, power_manager::PowerSupplyProperties::DISCHARGING)
          .SerializeAsString(),
      kPowerSupplyPollSignal);
  EXPECT_TRUE(
      power_supply_training_condition()->IsTrainingConditionSatisfied());

  // battery = 50, state = charging, satisfied.
  CreateSignalAndInvoke(
      GeneratePowerSupplyProto(50.0,
                               power_manager::PowerSupplyProperties::CHARGING)
          .SerializeAsString(),
      kPowerSupplyPollSignal);
  EXPECT_TRUE(
      power_supply_training_condition()->IsTrainingConditionSatisfied());

  // battery = 100, state = full, satisfied.
  CreateSignalAndInvoke(GeneratePowerSupplyProto(
                            100.0, power_manager::PowerSupplyProperties::FULL)
                            .SerializeAsString(),
                        kPowerSupplyPollSignal);
  EXPECT_TRUE(
      power_supply_training_condition()->IsTrainingConditionSatisfied());
}

TEST_F(PowerSupplyTrainingConditionTest, PowerSupplyNotSatisfied) {
  // battery = 80, state = discharging, unsatisfied.
  CreateSignalAndInvoke(
      GeneratePowerSupplyProto(
          80.0, power_manager::PowerSupplyProperties::DISCHARGING)
          .SerializeAsString(),
      kPowerSupplyPollSignal);
  EXPECT_FALSE(
      power_supply_training_condition()->IsTrainingConditionSatisfied());
}

TEST_F(PowerSupplyTrainingConditionTest, RespectBatterySaverMode) {
  // Meets the power supply requirements.
  CreateSignalAndInvoke(GeneratePowerSupplyProto(
                            100.0, power_manager::PowerSupplyProperties::FULL)
                            .SerializeAsString(),
                        kPowerSupplyPollSignal);
  EXPECT_TRUE(
      power_supply_training_condition()->IsTrainingConditionSatisfied());

  // When battery_saver is enabled, power supply conditions are not satisfied.
  power_manager::BatterySaverModeState battery_saver_mode_state;
  battery_saver_mode_state.set_enabled(true);
  CreateSignalAndInvoke(battery_saver_mode_state.SerializeAsString(),
                        kBatterySaverModeStateChanged);
  EXPECT_FALSE(
      power_supply_training_condition()->IsTrainingConditionSatisfied());

  battery_saver_mode_state.set_enabled(false);
  CreateSignalAndInvoke(battery_saver_mode_state.SerializeAsString(),
                        kBatterySaverModeStateChanged);
  EXPECT_TRUE(
      power_supply_training_condition()->IsTrainingConditionSatisfied());
}
}  // namespace federated
