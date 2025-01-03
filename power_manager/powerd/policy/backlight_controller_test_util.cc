// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/policy/backlight_controller_test_util.h"

#include <cmath>
#include <memory>

#include <chromeos/dbus/service_constants.h>
#include <base/check.h>
#include <dbus/message.h>
#include <gtest/gtest.h>

#include "power_manager/powerd/system/dbus_wrapper_stub.h"
#include "power_manager/proto_bindings/backlight.pb.h"

namespace power_manager::policy::test {

void CallIncreaseScreenBrightness(system::DBusWrapperStub* wrapper) {
  DCHECK(wrapper);
  dbus::MethodCall method_call(kPowerManagerInterface,
                               kIncreaseScreenBrightnessMethod);
  ASSERT_TRUE(wrapper->CallExportedMethodSync(&method_call));
}

void CallDecreaseScreenBrightness(system::DBusWrapperStub* wrapper,
                                  bool allow_off) {
  DCHECK(wrapper);
  dbus::MethodCall method_call(kPowerManagerInterface,
                               kDecreaseScreenBrightnessMethod);
  dbus::MessageWriter(&method_call).AppendBool(allow_off);
  ASSERT_TRUE(wrapper->CallExportedMethodSync(&method_call));
}

void CallSetScreenBrightness(
    system::DBusWrapperStub* wrapper,
    double percent,
    SetBacklightBrightnessRequest_Transition transition,
    SetBacklightBrightnessRequest_Cause cause) {
  DCHECK(wrapper);
  dbus::MethodCall method_call(kPowerManagerInterface,
                               kSetScreenBrightnessMethod);
  dbus::MessageWriter writer(&method_call);
  SetBacklightBrightnessRequest proto;
  proto.set_percent(percent);
  proto.set_transition(transition);
  proto.set_cause(cause);
  writer.AppendProtoAsArrayOfBytes(proto);
  ASSERT_TRUE(wrapper->CallExportedMethodSync(&method_call));
}

void CallSetAmbientLightSensorEnabled(
    system::DBusWrapperStub* wrapper,
    bool enabled,
    SetAmbientLightSensorEnabledRequest_Cause cause) {
  DCHECK(wrapper);
  dbus::MethodCall method_call(kPowerManagerInterface,
                               kSetAmbientLightSensorEnabledMethod);
  dbus::MessageWriter writer(&method_call);
  SetAmbientLightSensorEnabledRequest proto;
  proto.set_cause(cause);
  proto.set_sensor_enabled(enabled);
  writer.AppendProtoAsArrayOfBytes(proto);
  ASSERT_TRUE(wrapper->CallExportedMethodSync(&method_call));
}

BacklightBrightnessChange GetLastBrightnessChangedSignal(
    system::DBusWrapperStub* wrapper) {
  // Ensure at least one signal has been sent.
  size_t num_signals = wrapper->num_sent_signals();
  if (num_signals == 0) {
    EXPECT_GT(num_signals, 0) << "No brightness change signals have been sent.";
    return BacklightBrightnessChange{};
  }

  // Return the most recent signal.
  std::unique_ptr<dbus::Signal> signal;
  CHECK(wrapper->GetSentSignal(
      num_signals - 1, kKeyboardBrightnessChangedSignal, nullptr, &signal));
  BacklightBrightnessChange proto;
  CHECK(dbus::MessageReader(signal.get()).PopArrayOfBytesAsProto(&proto));
  return proto;
}

void CheckBrightnessChangedSignal(system::DBusWrapperStub* wrapper,
                                  size_t index,
                                  const std::string& signal_name,
                                  double brightness_percent,
                                  BacklightBrightnessChange_Cause cause) {
  std::unique_ptr<dbus::Signal> signal;
  ASSERT_TRUE(wrapper->GetSentSignal(index, signal_name, nullptr, &signal));

  BacklightBrightnessChange proto;
  ASSERT_TRUE(dbus::MessageReader(signal.get()).PopArrayOfBytesAsProto(&proto));
  EXPECT_DOUBLE_EQ(round(brightness_percent), round(proto.percent()));
  EXPECT_EQ(cause, proto.cause());
}

void CheckAmbientLightSensorEnabledChangedSignal(
    system::DBusWrapperStub* wrapper,
    size_t index,
    bool is_keyboard,
    bool expected_ambient_light_sensor_enabled,
    AmbientLightSensorChange_Cause expected_cause) {
  std::unique_ptr<dbus::Signal> signal;
  const char* signal_name =
      is_keyboard ? kKeyboardAmbientLightSensorEnabledChangedSignal
                  : kAmbientLightSensorEnabledChangedSignal;
  ASSERT_TRUE(wrapper->GetSentSignal(
      /*index=*/index,
      /*expected_signal_name=*/signal_name,
      /*protobuf_out=*/nullptr, /*signal_out=*/&signal));

  AmbientLightSensorChange proto;
  ASSERT_TRUE(dbus::MessageReader(signal.get()).PopArrayOfBytesAsProto(&proto));
  EXPECT_EQ(expected_ambient_light_sensor_enabled, proto.sensor_enabled());
  EXPECT_EQ(expected_cause, proto.cause());
}

}  // namespace power_manager::policy::test
