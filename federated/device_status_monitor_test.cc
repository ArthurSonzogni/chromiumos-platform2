// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <unordered_map>

#include <base/callback.h>
#include <base/check.h>
#include <base/memory/ref_counted.h>
#include <dbus/message.h>
#include <dbus/mock_bus.h>
#include <dbus/mock_object_proxy.h>
#include <dbus/power_manager/dbus-constants.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "federated/device_status_monitor.h"
#include "power_manager/proto_bindings/power_supply_properties.pb.h"

using power_manager::kPowerManagerInterface;
using power_manager::kPowerSupplyPollSignal;
using ::testing::_;
using ::testing::SaveArg;
using ::testing::StrictMock;

namespace federated {
namespace {

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

// Writes the given string (a serialized proto or "") to dbus signal.
void WriteSerializedProtoToSignal(const std::string& serialized_proto,
                                  dbus::Signal* signal) {
  dbus::MessageWriter writer(signal);
  writer.AppendArrayOfBytes(
      reinterpret_cast<const uint8_t*>(serialized_proto.data()),
      serialized_proto.size());
}

}  // namespace

class DeviceStatusMonitorTest : public ::testing::Test {
 public:
  DeviceStatusMonitorTest()
      : mock_dbus_(new StrictMock<dbus::MockBus>(dbus::Bus::Options())),
        dbus_object_proxy_(new StrictMock<dbus::MockObjectProxy>(
            mock_dbus_.get(),
            power_manager::kPowerManagerServiceName,
            dbus::ObjectPath(power_manager::kPowerManagerServicePath))) {}
  DeviceStatusMonitorTest(const DeviceStatusMonitorTest&) = delete;
  DeviceStatusMonitorTest& operator=(const DeviceStatusMonitorTest&) = delete;

  void SetUp() override {
    EXPECT_CALL(*mock_dbus_,
                GetObjectProxy(
                    power_manager::kPowerManagerServiceName,
                    dbus::ObjectPath(power_manager::kPowerManagerServicePath)))
        .WillOnce(Return(dbus_object_proxy_.get()));

    EXPECT_CALL(*dbus_object_proxy_,
                DoConnectToSignal(power_manager::kPowerManagerInterface,
                                  power_manager::kPowerSupplyPollSignal, _, _))
        .WillOnce(SaveArg<2>(
            &on_signal_callbacks_[power_manager::kPowerSupplyPollSignal]));

    device_status_monitor_ =
        std::make_unique<DeviceStatusMonitor>(mock_dbus_.get());
  }

  void InvokeSignal(const std::string& signal_name, dbus::Signal* signal) {
    ASSERT_TRUE(signal);
    auto callback_iter = on_signal_callbacks_.find(signal_name);
    ASSERT_NE(callback_iter, on_signal_callbacks_.end());
    callback_iter->second.Run(signal);
  }

  DeviceStatusMonitor* device_status_monitor() const {
    DCHECK(device_status_monitor_);
    return device_status_monitor_.get();
  }

 private:
  scoped_refptr<StrictMock<dbus::MockBus>> mock_dbus_;

  scoped_refptr<StrictMock<dbus::MockObjectProxy>> dbus_object_proxy_;

  std::unique_ptr<DeviceStatusMonitor> device_status_monitor_;

  // Currently DeviceStatusMonitor only connects to one signal, but may use more
  // in the future.
  std::unordered_map<std::string, base::Callback<void(dbus::Signal* signal)>>
      on_signal_callbacks_;
};

// Tests DeviceStatusMonitor is initialized correctly and can handle unexpected
// empty proto messages.
TEST_F(DeviceStatusMonitorTest, EmptySignal) {
  dbus::Signal signal(kPowerManagerInterface, kPowerSupplyPollSignal);

  // Initialized as false;
  EXPECT_FALSE(device_status_monitor()->TrainingConditionsSatisfied());

  // Invoke kPowerSupplyPollSignal without a valid proto message.
  InvokeSignal(kPowerSupplyPollSignal, &signal);
  EXPECT_FALSE(device_status_monitor()->TrainingConditionsSatisfied());

  // Invoke kPowerSupplyPollSignal with a valid empty proto message.
  WriteSerializedProtoToSignal("", &signal);
  InvokeSignal(kPowerSupplyPollSignal, &signal);
  EXPECT_FALSE(device_status_monitor()->TrainingConditionsSatisfied());
}

TEST_F(DeviceStatusMonitorTest, PowerSupplySatisfied) {
  // Note: here we must create new signals ranther than write to the old one,
  // because the WriteSerializedProtoToSignal is effectively "append", only the
  // first written string can be read by the DeviceStatusMonitor.

  // battery = 95, state = discharging, satisfied.
  dbus::Signal signal_1(kPowerManagerInterface, kPowerSupplyPollSignal);
  WriteSerializedProtoToSignal(
      GeneratePowerSupplyProto(
          95.0, power_manager::PowerSupplyProperties::DISCHARGING)
          .SerializeAsString(),
      &signal_1);
  InvokeSignal(kPowerSupplyPollSignal, &signal_1);
  EXPECT_TRUE(device_status_monitor()->TrainingConditionsSatisfied());

  // battery = 50, state = charging, satisfied.
  dbus::Signal signal_2(kPowerManagerInterface, kPowerSupplyPollSignal);
  WriteSerializedProtoToSignal(
      GeneratePowerSupplyProto(50.0,
                               power_manager::PowerSupplyProperties::CHARGING)
          .SerializeAsString(),
      &signal_2);
  InvokeSignal(kPowerSupplyPollSignal, &signal_2);
  EXPECT_TRUE(device_status_monitor()->TrainingConditionsSatisfied());

  // battery = 100, state = full, satisfied.
  dbus::Signal signal_3(kPowerManagerInterface, kPowerSupplyPollSignal);
  WriteSerializedProtoToSignal(
      GeneratePowerSupplyProto(100.0,
                               power_manager::PowerSupplyProperties::FULL)
          .SerializeAsString(),
      &signal_3);
  InvokeSignal(kPowerSupplyPollSignal, &signal_3);
  EXPECT_TRUE(device_status_monitor()->TrainingConditionsSatisfied());
}

TEST_F(DeviceStatusMonitorTest, PowerSupplyNotSatisfied) {
  // battery = 80, state = discharging, unsatisfied.
  dbus::Signal signal(kPowerManagerInterface, kPowerSupplyPollSignal);
  WriteSerializedProtoToSignal(
      GeneratePowerSupplyProto(
          80.0, power_manager::PowerSupplyProperties::DISCHARGING)
          .SerializeAsString(),
      &signal);
  InvokeSignal(kPowerSupplyPollSignal, &signal);
  EXPECT_FALSE(device_status_monitor()->TrainingConditionsSatisfied());
}

}  // namespace federated
