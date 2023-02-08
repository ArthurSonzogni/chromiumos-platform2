// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/policy/battery_saver_controller.h"

#include <dbus/power_manager/dbus-constants.h>
#include <gtest/gtest.h>
#include <power_manager/proto_bindings/battery_saver.pb.h>

#include "power_manager/powerd/system/dbus_wrapper_stub.h"
#include "power_manager/powerd/testing/test_environment.h"

namespace power_manager::policy {
namespace {

class BatterySaverControllerTest : public TestEnvironment {
 public:
  BatterySaverControllerTest() {
    controller_.Init(dbus_);
    dbus_.PublishService();
  }

  // Call the `GetBatterySaverModeState` D-Bus method.
  BatterySaverModeState CallGetBatterySaverModeState() {
    dbus::MethodCall method_call(kPowerManagerInterface,
                                 kGetBatterySaverModeState);
    std::unique_ptr<dbus::Response> response =
        dbus_.CallExportedMethodSync(&method_call);
    if (response == nullptr) {
      ADD_FAILURE() << "Call to `GetBatterySaverModeState` failed.";
      return {};
    }

    BatterySaverModeState result;
    if (!dbus::MessageReader(response.get()).PopArrayOfBytesAsProto(&result)) {
      ADD_FAILURE() << "Bad `GetBatterySaverModeState` result.";
      return {};
    }

    return result;
  }

  // Call the `SetBatterySaverModeState` D-Bus method with the
  // given request proto.
  void CallSetBatterySaverModeState(
      const SetBatterySaverModeStateRequest& request) {
    dbus::MethodCall method_call(kPowerManagerInterface,
                                 kSetBatterySaverModeState);
    dbus::MessageWriter writer(&method_call);
    writer.AppendProtoAsArrayOfBytes(request);
    std::unique_ptr<dbus::Response> response =
        dbus_.CallExportedMethodSync(&method_call);
    if (response == nullptr) {
      ADD_FAILURE() << "Call to `SetBatterySaverModeState` failed.";
      return;
    }
  }

  // Call the `SetBatterySaverModeState` D-Bus method with a
  // request to enable/disable BSM.
  void CallSetBatterySaverModeState(bool enabled) {
    SetBatterySaverModeStateRequest request;
    request.set_enabled(enabled);
    CallSetBatterySaverModeState(request);
  }

 protected:
  system::DBusWrapperStub dbus_;
  BatterySaverController controller_;
};

TEST_F(BatterySaverControllerTest, EnableDisable) {
  // Initial BSM state should be disabled.
  {
    BatterySaverModeState state = CallGetBatterySaverModeState();
    EXPECT_TRUE(state.has_enabled());
    EXPECT_FALSE(state.enabled());
  }

  // Enable BSM.
  {
    CallSetBatterySaverModeState(/*enabled=*/true);

    BatterySaverModeState state = CallGetBatterySaverModeState();
    EXPECT_TRUE(state.enabled());
  }

  // Disable BSM again.
  {
    CallSetBatterySaverModeState(/*enabled=*/false);

    BatterySaverModeState state = CallGetBatterySaverModeState();
    EXPECT_FALSE(state.enabled());
  }
}

TEST_F(BatterySaverControllerTest, BadSetBatterySaverModeState) {
  // Call `SetBatterySaverModeState` with no parameters.
  {
    dbus::MethodCall method_call(kPowerManagerInterface,
                                 kSetBatterySaverModeState);
    std::unique_ptr<dbus::Response> response =
        dbus_.CallExportedMethodSync(&method_call);
    EXPECT_EQ(response->GetErrorName(), DBUS_ERROR_INVALID_ARGS);
  }

  // Call `SetBatterySaverModeState` with an invalid proto.
  {
    dbus::MethodCall method_call(kPowerManagerInterface,
                                 kSetBatterySaverModeState);
    dbus::MessageWriter writer(&method_call);
    uint8_t nul_byte = 0;
    writer.AppendArrayOfBytes(&nul_byte, 1);
    std::unique_ptr<dbus::Response> response =
        dbus_.CallExportedMethodSync(&method_call);
    EXPECT_EQ(response->GetErrorName(), DBUS_ERROR_INVALID_ARGS);
  }
}

TEST_F(BatterySaverControllerTest, SignalSent) {
  // Expect a signal to be sent when the controller initially starts.
  {
    BatterySaverModeState state;
    EXPECT_TRUE(dbus_.GetSentSignal(/*index=*/0, kBatterySaverModeStateChanged,
                                    &state,
                                    /*signal_out=*/nullptr));
    EXPECT_FALSE(state.enabled());
    EXPECT_EQ(state.cause(), BatterySaverModeState::CAUSE_STATE_RESTORED);

    dbus_.ClearSentSignals();
  }

  // Enable BSM.
  {
    CallSetBatterySaverModeState(/*enabled=*/true);

    BatterySaverModeState state;
    EXPECT_TRUE(dbus_.GetSentSignal(/*index=*/0, kBatterySaverModeStateChanged,
                                    &state,
                                    /*signal_out=*/nullptr));
    EXPECT_TRUE(state.enabled());
    EXPECT_EQ(state.cause(), BatterySaverModeState::CAUSE_USER_ENABLED);

    dbus_.ClearSentSignals();
  }

  // Setting to the same state shouldn't send another signal.
  {
    CallSetBatterySaverModeState(/*enabled=*/true);
    EXPECT_EQ(dbus_.num_sent_signals(), 0);
  }

  // Disable BSM again.
  {
    CallSetBatterySaverModeState(/*enabled=*/false);

    BatterySaverModeState state;
    EXPECT_TRUE(dbus_.GetSentSignal(/*index=*/0, kBatterySaverModeStateChanged,
                                    &state,
                                    /*signal_out=*/nullptr));
    EXPECT_FALSE(state.enabled());
    EXPECT_EQ(state.cause(), BatterySaverModeState::CAUSE_USER_DISABLED);

    dbus_.ClearSentSignals();
  }
}

}  // namespace
}  // namespace power_manager::policy
