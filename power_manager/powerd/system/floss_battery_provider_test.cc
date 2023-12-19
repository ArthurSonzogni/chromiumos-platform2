// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/system/floss_battery_provider.h"

#include <memory>
#include <vector>

#include <gtest/gtest.h>
#include "power_manager/powerd/testing/test_environment.h"

#include "power_manager/powerd/system/dbus_wrapper_stub.h"

namespace power_manager::system {

class FlossBatteryProviderTest : public TestEnvironment {
 public:
  FlossBatteryProviderTest()
      : dbus_wrapper_stub_(new system::DBusWrapperStub()) {}
  FlossBatteryProviderTest(const FlossBatteryProviderTest&) = delete;
  FlossBatteryProviderTest& operator=(const FlossBatteryProviderTest&) = delete;

  ~FlossBatteryProviderTest() override = default;

  void SetUp() override {}

 protected:
  void TestInit(bool register_with_manager) {
    // Provide a mock DBus wrapper that can monitor and reply to DBus calls.
    dbus_wrapper_stub_->SetMethodCallback(
        base::BindRepeating(&FlossBatteryProviderTest::HandleDBusMethodCall,
                            base::Unretained(this)));

    // Initialize battery provider.
    floss_battery_provider_.Init(dbus_wrapper_stub_.get());
    EXPECT_FALSE(floss_battery_provider_.IsRegistered());

    // Notify the provider that the Bluetooth manager daemon is up.
    bluetooth_manager_proxy_ = dbus_wrapper_stub_.get()->GetObjectProxy(
        bluetooth_manager::kBluetoothManagerInterface,
        bluetooth_manager::kBluetoothManagerServicePath);
    if (!register_with_manager)
      return;
    dbus_wrapper_stub_->NotifyServiceAvailable(bluetooth_manager_proxy_,
                                               /*available=*/true);
    EXPECT_TRUE(dbus_wrapper_stub_->IsMethodExported(
        bluetooth_manager::kBluetoothManagerOnHciEnabledChanged));

    // Allow the provider to register itself with the Bluetooth manager.
    EXPECT_EQ(GetDBusMethodCalls(),
              bluetooth_manager::kBluetoothManagerRegisterCallback);
    floss_battery_provider_.OnRegisteredBluetoothManagerCallback(
        dbus::Response::CreateEmpty().get());
  }

  // Store the latest DBus call's method name and return the appropriate
  // response.
  std::unique_ptr<dbus::Response> HandleDBusMethodCall(dbus::ObjectProxy* proxy,
                                                       dbus::MethodCall* call) {
    std::string service_path = proxy->object_path().value();
    std::string method_name = call->GetMember();
    dbus_calls_.push_back(method_name);

    std::unique_ptr<dbus::Response> response =
        dbus::Response::FromMethodCall(call);
    if (service_path == bluetooth_manager::kBluetoothManagerServicePath) {
      // When the provider registers with the manager, ensure it presents the
      // correct callback service path.
      if (method_name == bluetooth_manager::kBluetoothManagerRegisterCallback) {
        dbus::MessageReader reader(call);
        dbus::ObjectPath object_path;
        reader.PopObjectPath(&object_path);
        EXPECT_EQ(object_path.value(), kPowerManagerServicePath);
      }
    }

    return response;
  }

  // Return a string list of the latest DBus calls.
  std::string GetDBusMethodCalls() {
    std::string calls = base::JoinString(dbus_calls_, ", ");
    dbus_calls_.clear();
    return calls;
  }

  // DBus communication.
  std::unique_ptr<system::DBusWrapperStub> dbus_wrapper_stub_;
  dbus::ObjectProxy* bluetooth_manager_proxy_;

  // Object to test.
  FlossBatteryProvider floss_battery_provider_;

  // Tracker variables.
  std::vector<std::string> dbus_calls_;
};

TEST_F(FlossBatteryProviderTest, Init) {
  TestInit(/*register_with_manager=*/true);
}

}  // namespace power_manager::system
