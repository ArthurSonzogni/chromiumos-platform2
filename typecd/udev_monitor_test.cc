// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "typecd/udev_monitor.h"

#include <base/files/file_util.h>
#include <base/test/task_environment.h>
#include <brillo/udev/mock_udev.h>
#include <brillo/udev/mock_udev_device.h>
#include <brillo/udev/mock_udev_enumerate.h>
#include <brillo/udev/mock_udev_list_entry.h>
#include <brillo/udev/mock_udev_monitor.h>
#include <brillo/unittest_utils.h>
#include <gtest/gtest.h>

#include "typecd/test_constants.h"

using testing::_;
using testing::ByMove;
using testing::Return;
using testing::StrEq;

namespace typecd {

namespace {

constexpr char kInvalidPortSysPath[] = "/sys/class/typec/a-yz";
constexpr char kFakeUsbSysPath[] = "/sys/bus/usb/devices/usb1/1-1";
constexpr char kInvalidUsbSysPath[] = "/sys/bus/usb/devices/usb1/a-yz";

// A really dumb observer to verify that UdevMonitor is invoking the right
// callbacks.
class TestObserver : public UdevMonitor::Observer {
 public:
  void OnPortAddedOrRemoved(const base::FilePath& path,
                            int port_num,
                            bool added) override {
    if (added)
      num_ports_++;
    else
      num_ports_--;
  };

  void OnPartnerAddedOrRemoved(const base::FilePath& path,
                               int port_num,
                               bool added) override {
    if (added)
      num_partners_++;
    else
      num_partners_--;
  };

  void OnPartnerAltModeAddedOrRemoved(const base::FilePath& path,
                                      int port_num,
                                      bool added) override{};

  void OnCableAddedOrRemoved(const base::FilePath& path,
                             int port_num,
                             bool added) override {
    if (added)
      num_cables_++;
    else
      num_cables_--;
  };

  void OnCablePlugAdded(const base::FilePath& path, int port_num) override {}

  void OnCableAltModeAdded(const base::FilePath& path, int port_num) override {
    num_cable_altmodes_++;
  };

  void OnPartnerChanged(int port_num) override { num_partner_change_events_++; }
  void OnPortChanged(int port_num) override {
    port_change_tracker_[port_num] = true;
  }

  int GetNumPorts() { return num_ports_; }
  int GetNumPartners() { return num_partners_; }
  int GetNumCables() { return num_cables_; }
  int GetNumCableAltModes() { return num_cable_altmodes_; }
  int GetNumPartnerChangeEvents() { return num_partner_change_events_; }

  // Helper to return whether a port change event was received for |port_num|.
  bool PortChanged(int port_num) {
    if (port_change_tracker_.find(port_num) == port_change_tracker_.end())
      return false;
    else
      return port_change_tracker_[port_num];
  }

  // Helper to reset |port_change_tracker_| state for |port_num|.
  void ResetPortChanged(int port_num) {
    port_change_tracker_[port_num] = false;
  }

 private:
  int num_partners_;
  int num_ports_;
  int num_cables_;
  int num_cable_altmodes_;
  int num_partner_change_events_;
  // Map to check whether a change was detected for a particular port:
  // Key = port number, Value = boolean value indicating whether a change event
  // was received.
  //
  // The map entries should be cleared before checking for any changes.
  std::map<int, bool> port_change_tracker_;
};

// Test UsbObserver to verify that right callback is called.
class TestUsbObserver : public UdevMonitor::UsbObserver {
 public:
  void OnDeviceAddedOrRemoved(const base::FilePath& path, bool added) {
    if (added)
      num_devices_++;
    else
      num_devices_--;
  }

  int GetNumDevices() { return num_devices_; }

 private:
  int num_devices_;
};

}  // namespace

class UdevMonitorTest : public ::testing::Test {
 public:
  UdevMonitorTest()
      : task_environment_(
            base::test::TaskEnvironment::MainThreadType::IO,
            base::test::TaskEnvironment::ThreadPoolExecutionMode::ASYNC) {}

 protected:
  void SetUp() override {
    observer_ = std::make_unique<TestObserver>();
    usb_observer_ = std::make_unique<TestUsbObserver>();

    monitor_ = std::make_unique<UdevMonitor>();
    monitor_->AddObserver(observer_.get());
    monitor_->AddUsbObserver(usb_observer_.get());
  }

  void TearDown() override {
    monitor_.reset();
    observer_.reset();
    usb_observer_.reset();
  }

  // Add a task environment to keep the FileDescriptorWatcher code happy.
  base::test::TaskEnvironment task_environment_;
  std::unique_ptr<TestObserver> observer_;
  std::unique_ptr<TestUsbObserver> usb_observer_;
  std::unique_ptr<UdevMonitor> monitor_;
};

TEST_F(UdevMonitorTest, TestBasic) {
  // Create the Mock Udev objects and function invocation expectations.
  auto list_entry2 = std::make_unique<brillo::MockUdevListEntry>();
  EXPECT_CALL(*list_entry2, GetName())
      .WillOnce(Return(kFakePort0PartnerSysPath));
  EXPECT_CALL(*list_entry2, GetNext()).WillOnce(Return(ByMove(nullptr)));

  auto list_entry1 = std::make_unique<brillo::MockUdevListEntry>();
  EXPECT_CALL(*list_entry1, GetName()).WillOnce(Return(kFakePort0SysPath));
  EXPECT_CALL(*list_entry1, GetNext())
      .WillOnce(Return(ByMove(std::move(list_entry2))));

  auto enumerate = std::make_unique<brillo::MockUdevEnumerate>();
  EXPECT_CALL(*enumerate, AddMatchSubsystem(StrEq(kUsbSubsystem)))
      .WillOnce(Return(true));
  EXPECT_CALL(*enumerate, AddMatchSubsystem(StrEq(kTypeCSubsystem)))
      .WillOnce(Return(true));
  EXPECT_CALL(*enumerate, ScanDevices()).WillOnce(Return(true));
  EXPECT_CALL(*enumerate, GetListEntry())
      .WillOnce(Return(ByMove(std::move(list_entry1))));

  auto udev = std::make_unique<brillo::MockUdev>();
  EXPECT_CALL(*udev, CreateEnumerate())
      .WillOnce(Return(ByMove(std::move(enumerate))));

  monitor_->SetUdev(std::move(udev));

  EXPECT_THAT(0, observer_->GetNumPorts());

  ASSERT_TRUE(monitor_->ScanDevices());

  EXPECT_THAT(1, observer_->GetNumPorts());
  EXPECT_THAT(1, observer_->GetNumPartners());
}

// Check that a port and partner can be detected after init. Also check whether
// a subsequent partner removal is detected correctly.
TEST_F(UdevMonitorTest, TestHotplug) {
  // Create a socket-pair; to help poke the udev monitoring logic.
  auto fds = std::make_unique<brillo::ScopedSocketPair>();

  // Fake the calls for port add.
  auto device_port = std::make_unique<brillo::MockUdevDevice>();
  EXPECT_CALL(*device_port, GetSysPath()).WillOnce(Return(kFakePort0SysPath));
  EXPECT_CALL(*device_port, GetAction()).WillOnce(Return("add"));

  // Fake the calls for partner add.
  auto device_partner_add = std::make_unique<brillo::MockUdevDevice>();
  EXPECT_CALL(*device_partner_add, GetSysPath())
      .WillOnce(Return(kFakePort0PartnerSysPath));
  EXPECT_CALL(*device_partner_add, GetAction()).WillOnce(Return("add"));

  // Fake the calls for partner remove.
  auto device_partner_remove = std::make_unique<brillo::MockUdevDevice>();
  EXPECT_CALL(*device_partner_remove, GetSysPath())
      .WillOnce(Return(kFakePort0PartnerSysPath));
  EXPECT_CALL(*device_partner_remove, GetAction()).WillOnce(Return("remove"));

  // Fake the calls for cable add.
  auto device_cable_add = std::make_unique<brillo::MockUdevDevice>();
  EXPECT_CALL(*device_cable_add, GetSysPath())
      .WillOnce(Return(kFakePort0CableSysPath));
  EXPECT_CALL(*device_cable_add, GetAction()).WillOnce(Return("add"));

  // Create the Mock Udev objects and function invocation expectations.
  auto monitor = std::make_unique<brillo::MockUdevMonitor>();
  EXPECT_CALL(*monitor,
              FilterAddMatchSubsystemDeviceType(StrEq(kUsbSubsystem), nullptr))
      .WillOnce(Return(true));
  EXPECT_CALL(*monitor, FilterAddMatchSubsystemDeviceType(
                            StrEq(kTypeCSubsystem), nullptr))
      .WillOnce(Return(true));
  EXPECT_CALL(*monitor, EnableReceiving()).WillOnce(Return(true));
  EXPECT_CALL(*monitor, GetFileDescriptor()).WillOnce(Return(fds->left));
  EXPECT_CALL(*monitor, ReceiveDevice())
      .WillOnce(Return(ByMove(std::move(device_port))))
      .WillOnce(Return(ByMove(std::move(device_partner_add))))
      .WillOnce(Return(ByMove(std::move(device_partner_remove))))
      .WillOnce(Return(ByMove(std::move(device_cable_add))));

  auto udev = std::make_unique<brillo::MockUdev>();
  EXPECT_CALL(*udev, CreateMonitorFromNetlink(StrEq(kUdevMonitorName)))
      .WillOnce(Return(ByMove(std::move(monitor))));

  monitor_->SetUdev(std::move(udev));

  EXPECT_THAT(0, observer_->GetNumPorts());
  EXPECT_THAT(0, observer_->GetNumCables());

  // Skip initial scanning, since we are only interested in testing hotplug.
  ASSERT_TRUE(monitor_->BeginMonitoring());

  // It's too tedious to poke the socket pair to actually trigger the
  // FileDescriptorWatcher without it running repeatedly.
  //
  // Instead we manually call HandleUdevEvent. Effectively this equivalent to
  // triggering the event handler using the FileDescriptorWatcher.
  monitor_->HandleUdevEvent();
  EXPECT_THAT(1, observer_->GetNumPorts());
  monitor_->HandleUdevEvent();
  EXPECT_THAT(1, observer_->GetNumPartners());
  monitor_->HandleUdevEvent();
  EXPECT_THAT(0, observer_->GetNumPartners());
  monitor_->HandleUdevEvent();
  EXPECT_THAT(1, observer_->GetNumCables());
}

// Test that the udev handler correctly handles invalid port sysfs paths.
TEST_F(UdevMonitorTest, TestInvalidPortSyspath) {
  // Create a socket-pair; to help poke the udev monitoring logic.
  auto fds = std::make_unique<brillo::ScopedSocketPair>();

  // Fake the calls for port add.
  auto device_port = std::make_unique<brillo::MockUdevDevice>();
  EXPECT_CALL(*device_port, GetSysPath()).WillOnce(Return(kInvalidPortSysPath));
  EXPECT_CALL(*device_port, GetAction()).WillOnce(Return("add"));

  // Create the Mock Udev objects and function invocation expectations.
  auto monitor = std::make_unique<brillo::MockUdevMonitor>();
  EXPECT_CALL(*monitor,
              FilterAddMatchSubsystemDeviceType(StrEq(kUsbSubsystem), nullptr))
      .WillOnce(Return(true));
  EXPECT_CALL(*monitor, FilterAddMatchSubsystemDeviceType(
                            StrEq(kTypeCSubsystem), nullptr))
      .WillOnce(Return(true));
  EXPECT_CALL(*monitor, EnableReceiving()).WillOnce(Return(true));
  EXPECT_CALL(*monitor, GetFileDescriptor()).WillOnce(Return(fds->left));
  EXPECT_CALL(*monitor, ReceiveDevice())
      .WillOnce(Return(ByMove(std::move(device_port))));

  auto udev = std::make_unique<brillo::MockUdev>();
  EXPECT_CALL(*udev, CreateMonitorFromNetlink(StrEq(kUdevMonitorName)))
      .WillOnce(Return(ByMove(std::move(monitor))));

  monitor_->SetUdev(std::move(udev));

  // Skip initial scanning, since we are only interested in testing hotplug.
  ASSERT_TRUE(monitor_->BeginMonitoring());

  // Manually call HandleUdevEvent. Effectively this equivalent to triggering
  // the event handler using the FileDescriptorWatcher.
  monitor_->HandleUdevEvent();
  EXPECT_THAT(0, observer_->GetNumPorts());
}

// Test that the monitor can detect cable creation and SOP' alternate mode
// addition. Also checks that an SOP'' alternate mode addition is ignored.
TEST_F(UdevMonitorTest, TestCableAndAltModeAddition) {
  // Create the Mock Udev objects and function invocation expectations.

  // Unsupported SOP'' alternate mode.
  auto list_entry3 = std::make_unique<brillo::MockUdevListEntry>();
  EXPECT_CALL(*list_entry3, GetName())
      .WillOnce(Return(kFakePort0SOPDoublePrimeAltModeSysPath));
  EXPECT_CALL(*list_entry3, GetNext())
      .WillOnce(Return(ByMove(std::move(nullptr))));

  // SOP' alternate mode.
  auto list_entry2 = std::make_unique<brillo::MockUdevListEntry>();
  EXPECT_CALL(*list_entry2, GetName())
      .WillOnce(Return(kFakePort0SOPPrimeAltModeSysPath));
  EXPECT_CALL(*list_entry2, GetNext())
      .WillOnce(Return(ByMove(std::move(list_entry3))));

  // Cable.
  auto list_entry1 = std::make_unique<brillo::MockUdevListEntry>();
  EXPECT_CALL(*list_entry1, GetName()).WillOnce(Return(kFakePort0CableSysPath));
  EXPECT_CALL(*list_entry1, GetNext())
      .WillOnce(Return(ByMove(std::move(list_entry2))));

  auto enumerate = std::make_unique<brillo::MockUdevEnumerate>();
  EXPECT_CALL(*enumerate, AddMatchSubsystem(StrEq(kUsbSubsystem)))
      .WillOnce(Return(true));
  EXPECT_CALL(*enumerate, AddMatchSubsystem(StrEq(kTypeCSubsystem)))
      .WillOnce(Return(true));
  EXPECT_CALL(*enumerate, ScanDevices()).WillOnce(Return(true));
  EXPECT_CALL(*enumerate, GetListEntry())
      .WillOnce(Return(ByMove(std::move(list_entry1))));

  auto udev = std::make_unique<brillo::MockUdev>();
  EXPECT_CALL(*udev, CreateEnumerate())
      .WillOnce(Return(ByMove(std::move(enumerate))));

  monitor_->SetUdev(std::move(udev));

  ASSERT_TRUE(monitor_->ScanDevices());

  EXPECT_THAT(1, observer_->GetNumCables());
  EXPECT_THAT(1, observer_->GetNumCableAltModes());
}

// Check that a basic partner change event gets detected correctly.
TEST_F(UdevMonitorTest, TestPartnerChanged) {
  // Create a socket-pair; to help poke the udev monitoring logic.
  auto fds = std::make_unique<brillo::ScopedSocketPair>();

  // Fake the calls for partner change.
  auto device_partner_change = std::make_unique<brillo::MockUdevDevice>();
  EXPECT_CALL(*device_partner_change, GetSysPath())
      .WillOnce(Return(kFakePort0PartnerSysPath));
  EXPECT_CALL(*device_partner_change, GetAction()).WillOnce(Return("change"));

  // Create the Mock Udev objects and function invocation expectations.
  auto monitor = std::make_unique<brillo::MockUdevMonitor>();
  EXPECT_CALL(*monitor,
              FilterAddMatchSubsystemDeviceType(StrEq(kUsbSubsystem), nullptr))
      .WillOnce(Return(true));
  EXPECT_CALL(*monitor, FilterAddMatchSubsystemDeviceType(
                            StrEq(kTypeCSubsystem), nullptr))
      .WillOnce(Return(true));
  EXPECT_CALL(*monitor, EnableReceiving()).WillOnce(Return(true));
  EXPECT_CALL(*monitor, GetFileDescriptor()).WillOnce(Return(fds->left));
  EXPECT_CALL(*monitor, ReceiveDevice())
      .WillOnce(Return(ByMove(std::move(device_partner_change))));

  auto udev = std::make_unique<brillo::MockUdev>();
  EXPECT_CALL(*udev, CreateMonitorFromNetlink(StrEq(kUdevMonitorName)))
      .WillOnce(Return(ByMove(std::move(monitor))));

  monitor_->SetUdev(std::move(udev));

  EXPECT_THAT(0, observer_->GetNumPartnerChangeEvents());

  // Skip initial scanning, since we are only interested in testing the change
  // event.
  ASSERT_TRUE(monitor_->BeginMonitoring());

  // It's too tedious to poke the socket pair to actually trigger the
  // FileDescriptorWatcher without it running repeatedly.
  //
  // Instead we manually call HandleUdevEvent. Effectively this is equivalent to
  // triggering the event handler using the FileDescriptorWatcher.
  monitor_->HandleUdevEvent();
  EXPECT_THAT(1, observer_->GetNumPartnerChangeEvents());
}

// Check that a basic port change event gets detected correctly.
TEST_F(UdevMonitorTest, TestPortChanged) {
  // Create a socket-pair; to help poke the udev monitoring logic.
  auto fds = std::make_unique<brillo::ScopedSocketPair>();

  // Fake the calls for port change.
  auto device_port_change = std::make_unique<brillo::MockUdevDevice>();
  EXPECT_CALL(*device_port_change, GetSysPath())
      .WillOnce(Return(kFakePort0SysPath));
  EXPECT_CALL(*device_port_change, GetAction()).WillOnce(Return("change"));

  // Create the Mock Udev objects and function invocation expectations.
  auto monitor = std::make_unique<brillo::MockUdevMonitor>();
  EXPECT_CALL(*monitor,
              FilterAddMatchSubsystemDeviceType(StrEq(kUsbSubsystem), nullptr))
      .WillOnce(Return(true));
  EXPECT_CALL(*monitor, FilterAddMatchSubsystemDeviceType(
                            StrEq(kTypeCSubsystem), nullptr))
      .WillOnce(Return(true));
  EXPECT_CALL(*monitor, EnableReceiving()).WillOnce(Return(true));
  EXPECT_CALL(*monitor, GetFileDescriptor()).WillOnce(Return(fds->left));
  EXPECT_CALL(*monitor, ReceiveDevice())
      .WillOnce(Return(ByMove(std::move(device_port_change))));

  auto udev = std::make_unique<brillo::MockUdev>();
  EXPECT_CALL(*udev, CreateMonitorFromNetlink(StrEq(kUdevMonitorName)))
      .WillOnce(Return(ByMove(std::move(monitor))));

  monitor_->SetUdev(std::move(udev));

  // Prep the observer state for future events.
  observer_->ResetPortChanged(0);
  EXPECT_FALSE(observer_->PortChanged(0));

  // Skip initial scanning, since we are only interested in testing the change
  // event.
  ASSERT_TRUE(monitor_->BeginMonitoring());

  // It's too tedious to poke the socket pair to actually trigger the
  // FileDescriptorWatcher without it running repeatedly.
  //
  // Instead we manually call HandleUdevEvent. Effectively this is equivalent to
  // triggering the event handler using the FileDescriptorWatcher.
  monitor_->HandleUdevEvent();
  EXPECT_TRUE(observer_->PortChanged(0));
}

// Check that a USB device can be detected after init.
TEST_F(UdevMonitorTest, TestUsbDeviceScan) {
  // Create the Mock Udev objects and function invocation expectations.
  auto list_entry = std::make_unique<brillo::MockUdevListEntry>();
  EXPECT_CALL(*list_entry, GetName()).WillOnce(Return(kFakeUsbSysPath));
  EXPECT_CALL(*list_entry, GetNext()).WillOnce(Return(ByMove(nullptr)));

  auto enumerate = std::make_unique<brillo::MockUdevEnumerate>();
  EXPECT_CALL(*enumerate, AddMatchSubsystem(StrEq(kUsbSubsystem)))
      .WillOnce(Return(true));
  EXPECT_CALL(*enumerate, AddMatchSubsystem(StrEq(kTypeCSubsystem)))
      .WillOnce(Return(true));
  EXPECT_CALL(*enumerate, ScanDevices()).WillOnce(Return(true));
  EXPECT_CALL(*enumerate, GetListEntry())
      .WillOnce(Return(ByMove(std::move(list_entry))));

  auto udev = std::make_unique<brillo::MockUdev>();
  EXPECT_CALL(*udev, CreateEnumerate())
      .WillOnce(Return(ByMove(std::move(enumerate))));

  monitor_->SetUdev(std::move(udev));

  ASSERT_TRUE(monitor_->ScanDevices());

  EXPECT_THAT(1, usb_observer_->GetNumDevices());
}

// Check that a USB device add/remove can be detected through monitoring.
TEST_F(UdevMonitorTest, TestUsbDeviceAddRemove) {
  // Create a socket-pair; to help poke the udev monitoring logic.
  auto fds = std::make_unique<brillo::ScopedSocketPair>();

  // Fake the calls for USB add.
  auto usb_device_add = std::make_unique<brillo::MockUdevDevice>();
  EXPECT_CALL(*usb_device_add, GetSysPath()).WillOnce(Return(kFakeUsbSysPath));
  EXPECT_CALL(*usb_device_add, GetAction()).WillOnce(Return("add"));

  // Fake the calls for USB remove.
  auto usb_device_remove = std::make_unique<brillo::MockUdevDevice>();
  EXPECT_CALL(*usb_device_remove, GetSysPath())
      .WillOnce(Return(kFakeUsbSysPath));
  EXPECT_CALL(*usb_device_remove, GetAction()).WillOnce(Return("remove"));

  // Create the Mock Udev objects and function invocation expectations.
  auto monitor = std::make_unique<brillo::MockUdevMonitor>();
  EXPECT_CALL(*monitor,
              FilterAddMatchSubsystemDeviceType(StrEq(kUsbSubsystem), nullptr))
      .WillOnce(Return(true));
  EXPECT_CALL(*monitor, FilterAddMatchSubsystemDeviceType(
                            StrEq(kTypeCSubsystem), nullptr))
      .WillOnce(Return(true));
  EXPECT_CALL(*monitor, EnableReceiving()).WillOnce(Return(true));
  EXPECT_CALL(*monitor, GetFileDescriptor()).WillOnce(Return(fds->left));
  EXPECT_CALL(*monitor, ReceiveDevice())
      .WillOnce(Return(ByMove(std::move(usb_device_add))))
      .WillOnce(Return(ByMove(std::move(usb_device_remove))));

  auto udev = std::make_unique<brillo::MockUdev>();
  EXPECT_CALL(*udev, CreateMonitorFromNetlink(StrEq(kUdevMonitorName)))
      .WillOnce(Return(ByMove(std::move(monitor))));

  monitor_->SetUdev(std::move(udev));

  EXPECT_THAT(0, usb_observer_->GetNumDevices());

  ASSERT_TRUE(monitor_->BeginMonitoring());

  // It's too tedious to poke the socket pair to actually trigger the
  // FileDescriptorWatcher without it running repeatedly.
  //
  // Instead we manually call HandleUdevEvent. Effectively this is equivalent to
  // triggering the event handler using the FileDescriptorWatcher.
  monitor_->HandleUdevEvent();
  EXPECT_THAT(1, usb_observer_->GetNumDevices());
  monitor_->HandleUdevEvent();
  EXPECT_THAT(0, usb_observer_->GetNumDevices());
}

// Test that invalid syspath does not make callback.
TEST_F(UdevMonitorTest, TestInvalidUsbDeviceSyspath) {
  // Create a socket-pair; to help poke the udev monitoring logic.
  auto fds = std::make_unique<brillo::ScopedSocketPair>();

  // Fake the calls for USB add.
  auto usb_device = std::make_unique<brillo::MockUdevDevice>();
  EXPECT_CALL(*usb_device, GetSysPath()).WillOnce(Return(kInvalidUsbSysPath));
  EXPECT_CALL(*usb_device, GetAction()).WillOnce(Return("add"));

  // Create the Mock Udev objects and function invocation expectations.
  auto monitor = std::make_unique<brillo::MockUdevMonitor>();
  EXPECT_CALL(*monitor,
              FilterAddMatchSubsystemDeviceType(StrEq(kUsbSubsystem), nullptr))
      .WillOnce(Return(true));
  EXPECT_CALL(*monitor, FilterAddMatchSubsystemDeviceType(
                            StrEq(kTypeCSubsystem), nullptr))
      .WillOnce(Return(true));
  EXPECT_CALL(*monitor, EnableReceiving()).WillOnce(Return(true));
  EXPECT_CALL(*monitor, GetFileDescriptor()).WillOnce(Return(fds->left));
  EXPECT_CALL(*monitor, ReceiveDevice())
      .WillOnce(Return(ByMove(std::move(usb_device))));

  auto udev = std::make_unique<brillo::MockUdev>();
  EXPECT_CALL(*udev, CreateMonitorFromNetlink(StrEq(kUdevMonitorName)))
      .WillOnce(Return(ByMove(std::move(monitor))));

  monitor_->SetUdev(std::move(udev));

  EXPECT_THAT(0, usb_observer_->GetNumDevices());

  ASSERT_TRUE(monitor_->BeginMonitoring());

  // Manually call HandleUdevEvent. Effectively this equivalent to triggering
  // the event handler using the FileDescriptorWatcher.
  monitor_->HandleUdevEvent();
  EXPECT_THAT(0, usb_observer_->GetNumDevices());
}

}  // namespace typecd
