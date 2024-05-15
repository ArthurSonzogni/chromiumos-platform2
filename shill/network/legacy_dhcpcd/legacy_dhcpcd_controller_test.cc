// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <utility>
#include <vector>

#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/functional/callback_helpers.h>
#include <base/test/task_environment.h>
#include <dbus/mock_bus.h>
#include <dbus/mock_object_proxy.h>
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <net-base/mock_process_manager.h>

#include "shill/network/dhcpcd_controller_interface.h"
#include "shill/network/legacy_dhcpcd/legacy_dhcpcd_controller.h"
#include "shill/network/legacy_dhcpcd/legacy_dhcpcd_listener.h"

namespace shill {
namespace {

using testing::_;
using testing::Return;

// The fake LegacyDHCPCDListener that exposes the callbacks.
class FakeLegacyDHCPCDListener : public LegacyDHCPCDListener {
 public:
  FakeLegacyDHCPCDListener(scoped_refptr<dbus::Bus> bus,
                           EventDispatcher* dispatcher,
                           EventSignalCB event_signal_cb,
                           StatusChangedCB status_changed_cb)
      : event_signal_cb_(std::move(event_signal_cb)),
        status_changed_cb_(std::move(status_changed_cb)) {}
  ~FakeLegacyDHCPCDListener() override = default;

  EventSignalCB event_signal_cb_;
  StatusChangedCB status_changed_cb_;
};

// The factory that creates FakeLegacyDHCPCDListener.
class FakeLegacyDHCPCDListenerFactory : public LegacyDHCPCDListenerFactory {
 public:
  FakeLegacyDHCPCDListenerFactory() = default;
  ~FakeLegacyDHCPCDListenerFactory() override = default;

  MOCK_METHOD(std::unique_ptr<LegacyDHCPCDListener>,
              Create,
              (scoped_refptr<dbus::Bus> bus,
               EventDispatcher* dispatcher,
               EventSignalCB event_signal_cb,
               StatusChangedCB status_changed_cb),
              (override));
};

// The mock client of the LegacyDHCPCDController.
class MockClient : public DHCPCDControllerInterface::EventHandler {
 public:
  MOCK_METHOD(void,
              OnDHCPEvent,
              (DHCPCDControllerInterface::EventReason, const KeyValueStore&),
              (override));
  MOCK_METHOD(void, OnProcessExited, (int, int), (override));
};

class LegacyDHCPCDControllerFactoryTest : public testing::Test {
 public:
  void SetUp() override {
    EXPECT_TRUE(temp_dir_.CreateUniqueTempDir());
    root_path_ = temp_dir_.GetPath();

    mock_bus_ = new dbus::MockBus(dbus::Bus::Options());
    mock_object_proxy_ =
        new dbus::MockObjectProxy(mock_bus_.get(), "org.chromium.dhcpcd",
                                  dbus::ObjectPath("/org/chromium/dhcpcd"));
    ON_CALL(*mock_bus_, GetObjectProxy)
        .WillByDefault(Return(mock_object_proxy_.get()));

    // Create the fake listener instance by injecting the factory.
    auto listener_factory = std::make_unique<FakeLegacyDHCPCDListenerFactory>();
    EXPECT_CALL(*listener_factory, Create)
        .WillOnce(
            [this](
                scoped_refptr<dbus::Bus> bus, EventDispatcher* dispatcher,
                FakeLegacyDHCPCDListenerFactory::EventSignalCB event_signal_cb,
                FakeLegacyDHCPCDListenerFactory::StatusChangedCB
                    status_changed_cb) {
              auto listener = std::make_unique<FakeLegacyDHCPCDListener>(
                  std::move(bus), dispatcher, std::move(event_signal_cb),
                  std::move(status_changed_cb));
              fake_listener_ = listener.get();
              return listener;
            });

    controller_factory_ = std::make_unique<LegacyDHCPCDControllerFactory>(
        nullptr, mock_bus_, &mock_process_manager_,
        std::move(listener_factory));
    EXPECT_NE(fake_listener_, nullptr);
    controller_factory_->set_root_for_testing(root_path_);
  }

  std::unique_ptr<DHCPCDControllerInterface> CreateControllerSync(
      int expected_pid, std::string_view expected_dbus_service_name) {
    const DHCPCDControllerInterface::Options options = {};

    // When creating a controller, the controller factory should create
    // the dhcpcd process in minijail.
    EXPECT_CALL(mock_process_manager_, StartProcessInMinijail)
        .WillOnce(Return(expected_pid));
    EXPECT_CALL(mock_process_manager_, UpdateExitCallback(expected_pid, _))
        .WillOnce(
            [this](pid_t pid,
                   net_base::MockProcessManager::ExitCallback new_callback) {
              process_exit_cb_ = std::move(new_callback);
              return true;
            });

    std::unique_ptr<DHCPCDControllerInterface> controller;
    controller_factory_->CreateAsync(
        "wlan0", Technology::kWiFi, options, &client_,
        base::BindOnce(
            [](std::unique_ptr<DHCPCDControllerInterface>* out,
               std::unique_ptr<DHCPCDControllerInterface> in) {
              *out = std::move(in);
            },
            &controller));

    // After receiving D-Bus signal, the controller should be returned
    // asynchronously.
    fake_listener_->status_changed_cb_.Run(expected_dbus_service_name,
                                           expected_pid,
                                           LegacyDHCPCDListener::Status::kInit);

    return controller;
  }

  void CreateTempFileInRoot(std::string_view file) {
    const base::FilePath path_in_root = root_path_.Append(file);
    EXPECT_TRUE(base::CreateDirectory(path_in_root.DirName()));
    EXPECT_EQ(0, base::WriteFile(path_in_root, "", 0));
  }

  bool FileExistsInRoot(std::string_view file) {
    return base::PathExists(root_path_.Append(file));
  }

  base::test::TaskEnvironment task_environment_;
  base::ScopedTempDir temp_dir_;
  base::FilePath root_path_;

  net_base::MockProcessManager mock_process_manager_;
  net_base::MockProcessManager::ExitCallback process_exit_cb_;
  scoped_refptr<dbus::MockBus> mock_bus_;
  scoped_refptr<dbus::MockObjectProxy> mock_object_proxy_;
  FakeLegacyDHCPCDListener* fake_listener_ = nullptr;
  std::unique_ptr<LegacyDHCPCDControllerFactory> controller_factory_;

  MockClient client_;
};

TEST_F(LegacyDHCPCDControllerFactoryTest, DhcpcdArguments) {
  constexpr int kPid = 4;

  const std::vector<
      std::pair<DHCPCDControllerInterface::Options, std::vector<std::string>>>
      kExpectedArgs = {
          {{},
           {"-B", "-i", "chromeos", "-q", "-4", "-o", "captive_portal_uri",
            "--nodelay", "wlan0"}},
          {{.hostname = "my_hostname"},
           {"-B", "-i", "chromeos", "-q", "-4", "-o", "captive_portal_uri",
            "--nodelay", "-h", "my_hostname", "wlan0"}},
          {{.use_arp_gateway = true},
           {"-B", "-i", "chromeos", "-q", "-4", "-o", "captive_portal_uri",
            "--nodelay", "-R", "--unicast", "wlan0"}},
          {{.use_rfc_8925 = true},
           {"-B", "-i", "chromeos", "-q", "-4", "-o", "captive_portal_uri",
            "--nodelay", "-o", "ipv6_only_preferred", "wlan0"}},
          {{.apply_dscp = true},
           {"-B", "-i", "chromeos", "-q", "-4", "-o", "captive_portal_uri",
            "--nodelay", "--apply_dscp", "wlan0"}},
      };
  for (const auto& [options, dhcpcd_args] : kExpectedArgs) {
    // When creating a controller, the controller factory should create
    // the dhcpcd process in minijail.
    EXPECT_CALL(mock_process_manager_,
                StartProcessInMinijail(_, base::FilePath("/sbin/dhcpcd"),
                                       dhcpcd_args, _, _, _))
        .WillOnce(Return(kPid));
    EXPECT_CALL(mock_process_manager_, UpdateExitCallback(kPid, _))
        .WillOnce(
            [this](pid_t pid,
                   net_base::MockProcessManager::ExitCallback new_callback) {
              process_exit_cb_ = std::move(new_callback);
              return true;
            });

    controller_factory_->CreateAsync("wlan0", Technology::kWiFi, options,
                                     &client_, base::DoNothing());
  }
}

TEST_F(LegacyDHCPCDControllerFactoryTest, CreateAndDestroyController) {
  constexpr int kPid = 4;
  constexpr std::string_view kDBusServiceName = ":1.25";

  std::unique_ptr<DHCPCDControllerInterface> controller =
      CreateControllerSync(kPid, kDBusServiceName);
  EXPECT_NE(controller, nullptr);

  // The dhcpcd process should be terminated when the controller is destroyed.
  EXPECT_CALL(mock_process_manager_, StopProcessAndBlock(kPid));
  controller.reset();

  // The handler should not receive any event after the controller is destroyed.
  EXPECT_CALL(
      client_,
      OnDHCPEvent(DHCPCDControllerInterface::EventReason::kIPv6OnlyPreferred,
                  _))
      .Times(0);
  fake_listener_->status_changed_cb_.Run(
      kDBusServiceName, kPid, LegacyDHCPCDListener::Status::kIPv6OnlyPreferred);
}

TEST_F(LegacyDHCPCDControllerFactoryTest, KillProcessWithPendingRequest) {
  constexpr int kPid = 4;

  // When creating a controller, the controller factory should create
  // the dhcpcd process in minijail.
  EXPECT_CALL(mock_process_manager_, StartProcessInMinijail)
      .WillOnce(Return(kPid));
  EXPECT_CALL(mock_process_manager_, UpdateExitCallback(kPid, _))
      .WillOnce(
          [this](pid_t pid,
                 net_base::MockProcessManager::ExitCallback new_callback) {
            process_exit_cb_ = std::move(new_callback);
            return true;
          });

  bool create_cb_called = false;
  controller_factory_->CreateAsync(
      "wlan0", Technology::kWiFi, {}, &client_,
      base::BindOnce(
          [](bool* create_cb_called,
             std::unique_ptr<DHCPCDControllerInterface> in) {
            *create_cb_called = true;
          },
          &create_cb_called));

  // The create callback should not be called if the dhcpcd process doesn't send
  // any signal.
  EXPECT_FALSE(create_cb_called);
  // The dhcpcd process should be killed when the factory is destroyed.
  EXPECT_CALL(mock_process_manager_, StopProcessAndBlock(kPid));
  controller_factory_.reset();
}

TEST_F(LegacyDHCPCDControllerFactoryTest, ProcessExitedBeforeSignal) {
  constexpr int kPid = 4;
  constexpr int kExitStatus = 6;

  // When creating a controller, the controller factory should create
  // the dhcpcd process in minijail.
  EXPECT_CALL(mock_process_manager_, StartProcessInMinijail)
      .WillOnce(Return(kPid));
  EXPECT_CALL(mock_process_manager_, UpdateExitCallback(kPid, _))
      .WillOnce(
          [this](pid_t pid,
                 net_base::MockProcessManager::ExitCallback new_callback) {
            process_exit_cb_ = std::move(new_callback);
            return true;
          });

  bool create_cb_called = false;
  std::unique_ptr<DHCPCDControllerInterface> controller;
  controller_factory_->CreateAsync(
      "wlan0", Technology::kWiFi, {}, &client_,
      base::BindOnce(
          [](bool* create_cb_called,
             std::unique_ptr<DHCPCDControllerInterface>* out,
             std::unique_ptr<DHCPCDControllerInterface> in) {
            *create_cb_called = true;
            *out = std::move(in);
          },
          &create_cb_called, &controller));

  // If the dhcpcd process is exited without sending signal, then the callback
  // of CreateAsync() method should return nullptr.
  std::move(process_exit_cb_).Run(kExitStatus);
  EXPECT_TRUE(create_cb_called);
  EXPECT_EQ(controller, nullptr);
}

TEST_F(LegacyDHCPCDControllerFactoryTest, CreateMultipleControllers) {
  constexpr int kPid1 = 4;
  constexpr int kPid2 = 6;
  constexpr std::string_view kDBusServiceName1 = ":1.25";
  constexpr std::string_view kDBusServiceName2 = ":1.27";

  std::unique_ptr<DHCPCDControllerInterface> controller1 =
      CreateControllerSync(kPid1, kDBusServiceName1);
  EXPECT_NE(controller1, nullptr);
  std::unique_ptr<DHCPCDControllerInterface> controller2 =
      CreateControllerSync(kPid2, kDBusServiceName2);
  EXPECT_NE(controller1, nullptr);

  // The dhcpcd process should be terminated when the controller is destroyed.
  EXPECT_CALL(mock_process_manager_, StopProcessAndBlock(kPid1));
  EXPECT_CALL(mock_process_manager_, StopProcessAndBlock(kPid2));
  controller_factory_.reset();
}

TEST_F(LegacyDHCPCDControllerFactoryTest, ProcessExited) {
  constexpr int kPid = 4;
  constexpr std::string_view kDBusServiceName = ":1.25";
  constexpr int kExitStatus = 3;

  std::unique_ptr<DHCPCDControllerInterface> controller =
      CreateControllerSync(kPid, kDBusServiceName);

  // When ProcessManager triggers the process exit callback, the factory should
  // notify the client by EventHandler::OnProcessExited().
  EXPECT_CALL(client_, OnProcessExited(kPid, kExitStatus));
  std::move(process_exit_cb_).Run(kExitStatus);
}

TEST_F(LegacyDHCPCDControllerFactoryTest, EventHandler) {
  constexpr int kPid = 4;
  constexpr std::string_view kDBusServiceName = ":1.25";

  std::unique_ptr<DHCPCDControllerInterface> controller =
      CreateControllerSync(kPid, kDBusServiceName);

  EXPECT_CALL(
      client_,
      OnDHCPEvent(DHCPCDControllerInterface::EventReason::kIPv6OnlyPreferred,
                  _));
  fake_listener_->status_changed_cb_.Run(
      kDBusServiceName, kPid, LegacyDHCPCDListener::Status::kIPv6OnlyPreferred);

  const KeyValueStore configuration = {};
  EXPECT_CALL(client_,
              OnDHCPEvent(DHCPCDControllerInterface::EventReason::kRebind, _));
  fake_listener_->event_signal_cb_.Run(
      kDBusServiceName, kPid, DHCPCDControllerInterface::EventReason::kRebind,
      configuration);
}

MATCHER_P2(IsDBusMethodCall, interface_name, method_name, "") {
  return arg->GetInterface() == interface_name &&
         arg->GetMember() == method_name;
}

TEST_F(LegacyDHCPCDControllerFactoryTest, Rebind) {
  constexpr int kPid = 4;
  constexpr std::string_view kDBusServiceName = ":1.25";

  std::unique_ptr<DHCPCDControllerInterface> controller =
      CreateControllerSync(kPid, kDBusServiceName);

  EXPECT_CALL(
      *mock_object_proxy_.get(),
      CallMethodAndBlock(IsDBusMethodCall("org.chromium.dhcpcd", "Rebind"), _))
      .WillOnce(Return(base::ok(dbus::Response::CreateEmpty())));
  EXPECT_TRUE(controller->Rebind());
}

TEST_F(LegacyDHCPCDControllerFactoryTest, Release) {
  constexpr int kPid = 4;
  constexpr std::string_view kDBusServiceName = ":1.25";

  std::unique_ptr<DHCPCDControllerInterface> controller =
      CreateControllerSync(kPid, kDBusServiceName);

  EXPECT_CALL(
      *mock_object_proxy_.get(),
      CallMethodAndBlock(IsDBusMethodCall("org.chromium.dhcpcd", "Release"), _))
      .WillOnce(Return(base::ok(dbus::Response::CreateEmpty())));
  EXPECT_TRUE(controller->Release());
}

TEST_F(LegacyDHCPCDControllerFactoryTest, DeleteEphemeralLeaseAndPidFile) {
  constexpr int kPid = 4;
  constexpr std::string_view kDBusServiceName = ":1.25";
  constexpr std::string_view kInterface = "wlan0";
  constexpr std::string_view kPidFile = "var/run/dhcpcd/dhcpcd-wlan0-4.pid";
  constexpr std::string_view kLeaseFile = "var/lib/dhcpcd/wlan0.lease";
  const DHCPCDControllerInterface::Options options = {};

  // When creating a controller, the controller factory should create
  // the dhcpcd process in minijail.
  EXPECT_CALL(mock_process_manager_, StartProcessInMinijail)
      .WillOnce(Return(kPid));
  EXPECT_CALL(mock_process_manager_, UpdateExitCallback).WillOnce(Return(true));

  std::unique_ptr<DHCPCDControllerInterface> controller;
  controller_factory_->CreateAsync(
      kInterface, Technology::kWiFi, options, &client_,
      base::BindOnce(
          [](std::unique_ptr<DHCPCDControllerInterface>* out,
             std::unique_ptr<DHCPCDControllerInterface> in) {
            *out = std::move(in);
          },
          &controller));

  // After receiving D-Bus signal, the controller should be returned
  // asynchronously.
  fake_listener_->status_changed_cb_.Run(kDBusServiceName, kPid,
                                         LegacyDHCPCDListener::Status::kInit);
  ASSERT_NE(controller, nullptr);

  CreateTempFileInRoot(kPidFile);
  CreateTempFileInRoot(kLeaseFile);
  EXPECT_TRUE(FileExistsInRoot(kPidFile));
  EXPECT_TRUE(FileExistsInRoot(kLeaseFile));

  // After the controller is destroyed, the pid file and the lease file should
  // be deleted.
  controller.reset();
  EXPECT_FALSE(FileExistsInRoot(kPidFile));
  EXPECT_FALSE(FileExistsInRoot(kLeaseFile));
}

}  // namespace
}  // namespace shill