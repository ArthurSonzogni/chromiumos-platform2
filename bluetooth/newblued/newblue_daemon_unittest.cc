// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "bluetooth/newblued/newblue_daemon.h"

#include <map>
#include <memory>
#include <string>
#include <utility>

#include <base/memory/ref_counted.h>
#include <base/message_loop/message_loop.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/mock_bus.h>
#include <dbus/mock_exported_object.h>
#include <dbus/object_manager.h>
#include <gtest/gtest.h>

#include "bluetooth/newblued/mock_libnewblue.h"
#include "bluetooth/newblued/mock_newblue.h"

using ::testing::_;
using ::testing::AnyNumber;
using ::testing::Return;
using ::testing::SaveArg;

namespace bluetooth {

namespace {

constexpr char kAdapterObjectPath[] = "/org/bluez/hci0";

constexpr char kTestSender[] = ":1.1";
constexpr int kTestSerial = 10;
constexpr char kTestDeviceAddress[] = "06:05:04:03:02:01";
constexpr char kTestDeviceObjectPath[] =
    "/org/bluez/hci0/dev_06_05_04_03_02_01";

void SaveResponse(std::unique_ptr<dbus::Response>* saved_response,
                  std::unique_ptr<dbus::Response> response) {
  *saved_response = std::move(response);
}

}  // namespace

class NewblueDaemonTest : public ::testing::Test {
 public:
  using MethodHandlerMap =
      std::map<std::string, dbus::ExportedObject::MethodCallCallback*>;

  void SetUp() override {
    bus_ = new dbus::MockBus(dbus::Bus::Options());
    EXPECT_CALL(*bus_, AssertOnOriginThread()).Times(AnyNumber());
    auto libnewblue = std::make_unique<MockLibNewblue>();
    libnewblue_ = libnewblue.get();
    auto newblue = std::make_unique<MockNewblue>(std::move(libnewblue));
    newblue_ = newblue.get();
    newblue_daemon_ = std::make_unique<NewblueDaemon>(std::move(newblue));
  }

  // The mocked dbus::ExportedObject::ExportMethod needs to call its callback.
  void StubExportMethod(
      const std::string& interface_name,
      const std::string& method_name,
      dbus::ExportedObject::MethodCallCallback method_call_callback,
      dbus::ExportedObject::OnExportedCallback on_exported_callback) {
    on_exported_callback.Run(interface_name, method_name, true /* success */);
  }

 protected:
  dbus::ExportedObject::MethodCallCallback* GetMethodHandler(
      MethodHandlerMap method_handlers, const std::string& method_name) {
    return base::ContainsKey(method_handlers, method_name)
               ? method_handlers[method_name]
               : &dummy_method_handler_;
  }

  // Expects that the standard methods on org.freedesktop.DBus.Properties
  // interface are exported.
  void ExpectPropertiesMethodsExported(
      scoped_refptr<dbus::MockExportedObject> exported_object) {
    EXPECT_CALL(*exported_object, ExportMethod(dbus::kPropertiesInterface,
                                               dbus::kPropertiesGet, _, _))
        .Times(1);
    EXPECT_CALL(*exported_object, ExportMethod(dbus::kPropertiesInterface,
                                               dbus::kPropertiesSet, _, _))
        .Times(1);
    EXPECT_CALL(*exported_object, ExportMethod(dbus::kPropertiesInterface,
                                               dbus::kPropertiesGetAll, _, _))
        .Times(1);
  }

  // Expects that the methods on org.bluez.Device1 interface are exported.
  void ExpectDeviceMethodsExported(
      scoped_refptr<dbus::MockExportedObject> exported_object) {
    EXPECT_CALL(*exported_object,
                ExportMethod(bluetooth_device::kBluetoothDeviceInterface,
                             bluetooth_device::kPair, _, _))
        .Times(1);
    EXPECT_CALL(*exported_object,
                ExportMethod(bluetooth_device::kBluetoothDeviceInterface,
                             bluetooth_device::kConnect, _, _))
        .Times(1);
  }

  scoped_refptr<dbus::MockExportedObject> SetupExportedRootObject() {
    dbus::ObjectPath root_path(
        newblue_object_manager::kNewblueObjectManagerServicePath);
    scoped_refptr<dbus::MockExportedObject> exported_root_object =
        new dbus::MockExportedObject(bus_.get(), root_path);
    EXPECT_CALL(*bus_, GetExportedObject(root_path))
        .WillRepeatedly(Return(exported_root_object.get()));
    return exported_root_object;
  }

  void ExpectTestInit(
      scoped_refptr<dbus::MockExportedObject> exported_root_object) {
    EXPECT_CALL(*bus_,
                RequestOwnershipAndBlock(
                    newblue_object_manager::kNewblueObjectManagerServiceName,
                    dbus::Bus::REQUIRE_PRIMARY))
        .WillOnce(Return(true));

    // Standard methods on org.freedesktop.DBus.ObjectManager interface should
    // be exported.
    EXPECT_CALL(*exported_root_object,
                ExportMethod(dbus::kObjectManagerInterface,
                             dbus::kObjectManagerGetManagedObjects, _, _))
        .Times(1);
    EXPECT_CALL(*exported_root_object,
                ExportMethod(dbus::kObjectManagerInterface,
                             dbus::kObjectManagerInterfacesAdded, _, _))
        .Times(1);
    EXPECT_CALL(*exported_root_object,
                ExportMethod(dbus::kObjectManagerInterface,
                             dbus::kObjectManagerInterfacesRemoved, _, _))
        .Times(1);
    // Standard methods on org.freedesktop.DBus.Properties interface should be
    // exported.
    ExpectPropertiesMethodsExported(exported_root_object);
  }

  void TestInit(scoped_refptr<dbus::MockExportedObject> exported_root_object) {
    ExpectTestInit(exported_root_object);

    EXPECT_CALL(*newblue_, Init()).WillOnce(Return(true));
    EXPECT_CALL(*newblue_, ListenReadyForUp(_)).WillOnce(Return(true));
    EXPECT_TRUE(newblue_daemon_->Init(
        bus_, nullptr /* no need to access the delegator */));
  }

  void TestAdapterBringUp(
      scoped_refptr<dbus::MockExportedObject> exported_adapter_object,
      MethodHandlerMap adapter_method_handlers) {
    // Some properties are expected to be exported on the adapter object.
    EXPECT_CALL(
        *exported_adapter_object,
        ExportMethod(dbus::kPropertiesInterface, dbus::kPropertiesGetAll, _, _))
        .Times(AnyNumber());
    EXPECT_CALL(
        *exported_adapter_object,
        ExportMethod(dbus::kPropertiesInterface, dbus::kPropertiesGet, _, _))
        .Times(AnyNumber());
    EXPECT_CALL(
        *exported_adapter_object,
        ExportMethod(dbus::kPropertiesInterface, dbus::kPropertiesSet, _, _))
        .Times(AnyNumber());

    // org.bluez.Adapter1 methods
    EXPECT_CALL(*exported_adapter_object,
                ExportMethod(bluetooth_adapter::kBluetoothAdapterInterface,
                             bluetooth_adapter::kStartDiscovery, _, _))
        .WillOnce(DoAll(
            SaveArg<2>(GetMethodHandler(adapter_method_handlers,
                                        bluetooth_adapter::kStartDiscovery)),
            Invoke(this, &NewblueDaemonTest::StubExportMethod)));

    EXPECT_CALL(*exported_adapter_object,
                ExportMethod(bluetooth_adapter::kBluetoothAdapterInterface,
                             bluetooth_adapter::kStopDiscovery, _, _))
        .WillOnce(DoAll(
            SaveArg<2>(GetMethodHandler(adapter_method_handlers,
                                        bluetooth_adapter::kStopDiscovery)),
            Invoke(this, &NewblueDaemonTest::StubExportMethod)));

    EXPECT_CALL(*newblue_, BringUp()).WillOnce(Return(true));
    newblue_daemon_->OnHciReadyForUp();
  }

  base::MessageLoop message_loop_;
  scoped_refptr<dbus::MockBus> bus_;
  std::unique_ptr<NewblueDaemon> newblue_daemon_;
  MockNewblue* newblue_;
  MockLibNewblue* libnewblue_;
  dbus::ExportedObject::MethodCallCallback dummy_method_handler_;
};

TEST_F(NewblueDaemonTest, InitFailed) {
  scoped_refptr<dbus::MockExportedObject> exported_root_object =
      SetupExportedRootObject();

  // Newblue::Init() fails
  ExpectTestInit(exported_root_object);
  EXPECT_CALL(*newblue_, Init()).WillOnce(Return(false));
  EXPECT_FALSE(newblue_daemon_->Init(
      bus_, nullptr /* no need to access the delegator */));

  // Newblue::ListenReadyForUp() fails
  ExpectTestInit(exported_root_object);
  EXPECT_CALL(*newblue_, Init()).WillOnce(Return(true));
  EXPECT_CALL(*newblue_, ListenReadyForUp(_)).WillOnce(Return(false));
  EXPECT_FALSE(newblue_daemon_->Init(
      bus_, nullptr /* no need to access the delegator */));

  // Shutdown now to make sure ExportedObjectManagerWrapper is destructed first
  // before the mocked objects.
  newblue_daemon_->Shutdown();
}

TEST_F(NewblueDaemonTest, InitSuccessAndBringUp) {
  scoped_refptr<dbus::MockExportedObject> exported_root_object =
      SetupExportedRootObject();

  dbus::ObjectPath adapter_object_path(kAdapterObjectPath);
  scoped_refptr<dbus::MockExportedObject> exported_adapter_object =
      new dbus::MockExportedObject(bus_.get(), adapter_object_path);
  EXPECT_CALL(*bus_, GetExportedObject(adapter_object_path))
      .WillOnce(Return(exported_adapter_object.get()));

  TestInit(exported_root_object);

  MethodHandlerMap adapter_method_handlers;
  TestAdapterBringUp(exported_adapter_object, adapter_method_handlers);

  EXPECT_CALL(*exported_adapter_object, Unregister()).Times(1);
  EXPECT_CALL(*exported_root_object, Unregister()).Times(1);
  // Shutdown now to make sure ExportedObjectManagerWrapper is destructed first
  // before the mocked objects.
  newblue_daemon_->Shutdown();
}

TEST_F(NewblueDaemonTest, DiscoveryAPI) {
  scoped_refptr<dbus::MockExportedObject> exported_root_object =
      SetupExportedRootObject();

  dbus::ObjectPath adapter_object_path(kAdapterObjectPath);
  scoped_refptr<dbus::MockExportedObject> exported_adapter_object =
      new dbus::MockExportedObject(bus_.get(), adapter_object_path);
  EXPECT_CALL(*bus_, GetExportedObject(adapter_object_path))
      .WillOnce(Return(exported_adapter_object.get()));

  TestInit(exported_root_object);

  dbus::ExportedObject::MethodCallCallback start_discovery_handler;
  dbus::ExportedObject::MethodCallCallback stop_discovery_handler;
  MethodHandlerMap adapter_method_handlers;
  adapter_method_handlers[bluetooth_adapter::kStartDiscovery] =
      &start_discovery_handler;
  adapter_method_handlers[bluetooth_adapter::kStopDiscovery] =
      &stop_discovery_handler;
  TestAdapterBringUp(exported_adapter_object, adapter_method_handlers);

  ASSERT_FALSE(start_discovery_handler.is_null());
  ASSERT_FALSE(stop_discovery_handler.is_null());

  // StartDiscovery
  dbus::MethodCall start_discovery_method_call(
      bluetooth_adapter::kBluetoothAdapterInterface,
      bluetooth_adapter::kStartDiscovery);
  start_discovery_method_call.SetPath(dbus::ObjectPath(kAdapterObjectPath));
  start_discovery_method_call.SetSender(kTestSender);
  start_discovery_method_call.SetSerial(kTestSerial);
  std::unique_ptr<dbus::Response> start_discovery_response;

  Newblue::DeviceDiscoveredCallback on_device_discovered;
  EXPECT_CALL(*newblue_, StartDiscovery(_))
      .WillOnce(DoAll(SaveArg<0>(&on_device_discovered), Return(true)));
  start_discovery_handler.Run(
      &start_discovery_method_call,
      base::Bind(&SaveResponse, &start_discovery_response));
  EXPECT_EQ("", start_discovery_response->GetErrorName());
  ASSERT_FALSE(on_device_discovered.is_null());

  // Device discovered.
  dbus::ObjectPath device_object_path(kTestDeviceObjectPath);
  scoped_refptr<dbus::MockExportedObject> exported_device_object =
      new dbus::MockExportedObject(bus_.get(), device_object_path);
  EXPECT_CALL(*bus_, GetExportedObject(device_object_path))
      .WillOnce(Return(exported_device_object.get()));
  ExpectDeviceMethodsExported(exported_device_object);
  ExpectPropertiesMethodsExported(exported_device_object);
  Device device(kTestDeviceAddress);
  on_device_discovered.Run(device);

  // StopDiscovery
  dbus::MethodCall stop_discovery_method_call(
      bluetooth_adapter::kBluetoothAdapterInterface,
      bluetooth_adapter::kStopDiscovery);
  stop_discovery_method_call.SetPath(dbus::ObjectPath(kAdapterObjectPath));
  stop_discovery_method_call.SetSender(kTestSender);
  stop_discovery_method_call.SetSerial(kTestSerial);
  std::unique_ptr<dbus::Response> stop_discovery_response;
  EXPECT_CALL(*newblue_, StopDiscovery()).WillOnce(Return(true));
  stop_discovery_handler.Run(
      &stop_discovery_method_call,
      base::Bind(&SaveResponse, &stop_discovery_response));
  EXPECT_EQ("", stop_discovery_response->GetErrorName());

  EXPECT_CALL(*exported_adapter_object, Unregister()).Times(1);
  EXPECT_CALL(*exported_root_object, Unregister()).Times(1);
  // Shutdown now to make sure ExportedObjectManagerWrapper is destructed first
  // before the mocked objects.
  newblue_daemon_->Shutdown();
}

}  // namespace bluetooth
