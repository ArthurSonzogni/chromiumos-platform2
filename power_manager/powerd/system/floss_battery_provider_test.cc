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
    EXPECT_TRUE(dbus_wrapper_stub_->IsMethodExported(
        FlossBatteryProvider::kFlossBatteryProviderManagerRefreshBatteryInfo));
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
    if (service_path ==
        battery_manager::kFlossBatteryProviderManagerServicePath) {
      // When the provider sends battery data, ensure the sent data is in a
      // valid format.
      if (method_name == FlossBatteryProvider::
                             kFlossBatteryProviderManagerUpdateDeviceBattery) {
        dbus::MessageReader reader(call);
        uint32_t battery_provider_id;
        EXPECT_TRUE(reader.PopUint32(&battery_provider_id));
        EXPECT_EQ(battery_provider_id, battery_provider_id_);

        // Unroll the BatterySet object.
        dbus::MessageReader battery_set(nullptr);
        EXPECT_TRUE(reader.PopArray(&battery_set));
        EXPECT_DICT_ENTRY(battery_set, "address", address_);
        EXPECT_DICT_ENTRY(battery_set, "source_uuid", source_uuid_);
        EXPECT_DICT_ENTRY(battery_set, "source_info", source_info_);

        // Unroll the Battery object.
        std::string result;
        dbus::MessageReader dict_reader(nullptr);
        dbus::MessageReader variant_reader(nullptr);
        dbus::MessageReader array_reader(nullptr);
        dbus::MessageReader battery(nullptr);

        EXPECT_TRUE(battery_set.PopDictEntry(&dict_reader));
        EXPECT_TRUE(dict_reader.PopString(&result));
        EXPECT_EQ(result, "batteries");
        EXPECT_TRUE(dict_reader.PopVariant(&variant_reader));
        EXPECT_TRUE(variant_reader.PopArray(&array_reader));
        EXPECT_TRUE(array_reader.PopArray(&battery));
        EXPECT_DICT_ENTRY(battery, "percentage", level_);
        EXPECT_DICT_ENTRY(battery, "variant", "");

        // Ensure the entire object has been processed.
        EXPECT_FALSE(battery.HasMoreData());
        EXPECT_FALSE(array_reader.HasMoreData());
        EXPECT_FALSE(variant_reader.HasMoreData());
        EXPECT_FALSE(dict_reader.HasMoreData());
        EXPECT_FALSE(reader.HasMoreData());
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

  // Overloaded function to unroll a DBus dict entry object and ensure its
  // contents are correct.
  void EXPECT_DICT_ENTRY(dbus::MessageReader& reader,
                         std::string key,
                         std::string value) {
    std::string k;
    std::string v;
    dbus::MessageReader dict_reader(nullptr);
    reader.PopDictEntry(&dict_reader);
    EXPECT_TRUE(dict_reader.PopString(&k));
    EXPECT_TRUE(dict_reader.PopVariantOfString(&v));
    EXPECT_EQ(k, key);
    EXPECT_EQ(v, value);
    EXPECT_FALSE(dict_reader.HasMoreData());
  }

  // Overloaded function to unroll a DBus dict entry object and ensure its
  // contents are correct.
  void EXPECT_DICT_ENTRY(dbus::MessageReader& reader,
                         std::string key,
                         uint32_t value) {
    std::string k;
    uint32_t v;
    dbus::MessageReader dict_reader(nullptr);
    reader.PopDictEntry(&dict_reader);
    EXPECT_TRUE(dict_reader.PopString(&k));
    EXPECT_TRUE(dict_reader.PopVariantOfUint32(&v));
    EXPECT_EQ(k, key);
    EXPECT_EQ(v, value);
    EXPECT_FALSE(dict_reader.HasMoreData());
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

  // Trigger the provider to register the provider to send a battery status
  // update.
  void UpdateDeviceBattery(const std::string& address, int level) {
    floss_battery_provider_.UpdateDeviceBattery(address, level);
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
  std::string address_ = "01-23-45-67-89-AB";
  std::string source_uuid_ = "6cb01dc5-326f-4e31-b06f-126fce10b3ff";
  std::string source_info_ = "HID";
  int level_ = 97;
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

// Ensure the provider can successfully send a battery update.
TEST_F(FlossBatteryProviderTest, BatteryUpdate) {
  TestInit(true, true);

  // Notify the provider about a battery status update.
  // Note: Battery data validation is done in `HandleDBusMethodCall`
  UpdateDeviceBattery(address_, level_);

  // Ensure the update was sent to the Bluetooth daemon.
  EXPECT_EQ(
      GetDBusMethodCalls(),
      FlossBatteryProvider::kFlossBatteryProviderManagerUpdateDeviceBattery);
}

// Ensure the provider is not sending battery data or crashing if the manager is
// not present.
TEST_F(FlossBatteryProviderTest, ManagerNotPresent) {
  TestInit(/*register_with_manager=*/false, true);

  // Shouldn't be able to send any battery updates.
  EXPECT_FALSE(IsRegistered());
  UpdateDeviceBattery(address_, level_);
  EXPECT_EQ(GetDBusMethodCalls(), "");

  // Resetting shouldn't make a difference.
  Reset(/*interface_is_available=*/false);
  UpdateDeviceBattery(address_, level_);
  EXPECT_EQ(GetDBusMethodCalls(), "");
  EXPECT_FALSE(IsRegistered());
}

// Ensure the provider is not sending battery data or crashing if the adapter is
// not present.
TEST_F(FlossBatteryProviderTest, AdapterNotPresent) {
  TestInit(true, /*register_with_adapter=*/false);

  // Resetting shouldn't cause a crash.
  Reset(/*interface_is_available=*/false);
  EXPECT_EQ(GetDBusMethodCalls(), "");

  // Shouldn't be able to send any battery updates.
  UpdateDeviceBattery(address_, level_);
  EXPECT_EQ(GetDBusMethodCalls(), "");
  EXPECT_FALSE(IsRegistered());
}

}  // namespace power_manager::system
