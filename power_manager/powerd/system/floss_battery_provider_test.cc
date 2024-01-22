// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/system/floss_battery_provider.h"

#include <memory>
#include <vector>

#include <dbus/mock_exported_object.h>
#include <dbus/mock_object_manager.h>
#include <dbus/mock_object_proxy.h>
#include <gtest/gtest.h>
#include "power_manager/powerd/testing/test_environment.h"

#include "power_manager/powerd/system/dbus_wrapper_stub.h"

using ::testing::_;
using ::testing::Return;

namespace power_manager::system {

namespace {

void DoNothing(std::unique_ptr<dbus::Response> response) {}

}  // namespace

class FlossBatteryProviderTest : public TestEnvironment {
 public:
  FlossBatteryProviderTest()
      : dbus_wrapper_stub_(new system::DBusWrapperStub()) {}
  FlossBatteryProviderTest(const FlossBatteryProviderTest&) = delete;
  FlossBatteryProviderTest& operator=(const FlossBatteryProviderTest&) = delete;

  ~FlossBatteryProviderTest() override = default;

  void SetUp() override {}

 protected:
  void TestInit(bool register_with_manager, bool register_with_adapter) {
    // Provide a mock DBus wrapper that can monitor and reply to DBus calls.
    dbus_wrapper_stub_->SetMethodCallback(
        base::BindRepeating(&FlossBatteryProviderTest::HandleDBusMethodCall,
                            base::Unretained(this)));

    // Initialize battery provider.
    floss_battery_provider_.Init(dbus_wrapper_stub_.get());
    EXPECT_FALSE(floss_battery_provider_.IsRegistered());

    // Notify the provider that the Bluetooth manager daemon is up.
    bluetooth_manager_proxy_ = dbus_wrapper_stub_.get()->GetObjectProxy(
        bluetooth_manager::kBluetoothManagerServiceName,
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

    // Notify the provider that the Bluetooth adapter daemon is up.
    bluetooth_adapter_proxy_ = dbus_wrapper_stub_.get()->GetObjectProxy(
        battery_manager::kFlossBatteryProviderManagerServiceName,
        battery_manager::kFlossBatteryProviderManagerServicePath);
    if (!register_with_adapter)
      return;
    dbus_wrapper_stub_->NotifyInterfaceAvailable(
        battery_manager::kFlossBatteryProviderManagerInterface, true);

    // Ensure the provider registers itself with the Floss daemon.
    EXPECT_EQ(GetDBusMethodCalls(),
              FlossBatteryProvider::
                  kFlossBatteryProviderManagerRegisterBatteryProvider);
    if (!register_with_adapter)
      return;
    RegisterAsBatteryProvider();
    EXPECT_TRUE(floss_battery_provider_.IsRegistered());
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

  // Return whether or not the battery provider is registered and ready to send
  // battery data.
  bool IsRegistered() { return floss_battery_provider_.IsRegistered(); }

  // Wrapper to reset the provider.
  void Reset(bool interface_is_available) {
    floss_battery_provider_.Reset();
    if (interface_is_available) {
      dbus_wrapper_stub_->NotifyInterfaceAvailable(
          battery_manager::kFlossBatteryProviderManagerInterface, true);
    }
    return;
  }

  // Trigger an incoming DBus call that mocks Bluetooth being toggled.
  void HciEnabledChanged(bool enabled) {
    auto method_call = std::make_unique<dbus::MethodCall>(
        kPowerManagerInterface,
        bluetooth_manager::kBluetoothManagerOnHciEnabledChanged);
    dbus::MessageWriter writer(method_call.get());
    writer.AppendInt32(hci_interface_);
    writer.AppendBool(enabled);
    floss_battery_provider_.OnHciEnabledChanged(method_call.get(),
                                                base::BindOnce(&DoNothing));
  }

  // Trigger the provider to register the provider as a battery provider.
  void RegisterAsBatteryProvider() {
    std::unique_ptr<dbus::Response> response = dbus::Response::CreateEmpty();
    dbus::MessageWriter writer(response.get());
    writer.AppendUint32(battery_provider_id_);
    floss_battery_provider_.OnRegisteredAsBatteryProvider(response.get());
  }

  // Constants.
  uint32_t battery_provider_id_ = 42;
  int32_t hci_interface_ = 0;

  // DBus communication.
  std::unique_ptr<system::DBusWrapperStub> dbus_wrapper_stub_;
  dbus::ObjectProxy* bluetooth_manager_proxy_;
  dbus::ObjectProxy* bluetooth_adapter_proxy_;

  // Object to test.
  FlossBatteryProvider floss_battery_provider_;

  // Tracker variables.
  std::vector<std::string> dbus_calls_;
};

// Ensure the battery provider attempts to re-register itself with the bluetooth
// adapter when the adapter is enabled or disabled.
TEST_F(FlossBatteryProviderTest, BluetoothAdapterEnabled) {
  TestInit(true, true);

  // Toggle Bluetooth off and back on.
  HciEnabledChanged(/*enabled=*/false);
  EXPECT_FALSE(IsRegistered());
  HciEnabledChanged(/*enabled=*/true);

  // Bluetooth was turned on, so the service is available again.
  dbus_wrapper_stub_->NotifyInterfaceAvailable(
      battery_manager::kFlossBatteryProviderManagerInterface, true);

  // Ensure the provider successfully re-registers.
  EXPECT_EQ(GetDBusMethodCalls(),
            FlossBatteryProvider::
                kFlossBatteryProviderManagerRegisterBatteryProvider);
  RegisterAsBatteryProvider();
  EXPECT_TRUE(IsRegistered());
}

// Ensure the provider is not sending battery data or crashing if the adapter is
// not present.
TEST_F(FlossBatteryProviderTest, AdapterNotPresent) {
  TestInit(true, /*register_with_adapter=*/false);
  EXPECT_FALSE(IsRegistered());

  // Resetting shouldn't cause a crash.
  Reset(/*interface_is_available=*/false);
  EXPECT_EQ(GetDBusMethodCalls(), "");
  EXPECT_FALSE(IsRegistered());
}

}  // namespace power_manager::system
