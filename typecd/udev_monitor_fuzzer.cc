// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "typecd/udev_monitor.h"

#include <base/logging.h>
#include <base/test/task_environment.h>
#include <brillo/udev/mock_udev.h>
#include <brillo/udev/mock_udev_device.h>
#include <brillo/udev/mock_udev_enumerate.h>
#include <brillo/udev/mock_udev_list_entry.h>
#include <brillo/udev/mock_udev_monitor.h>
#include "fuzzer/FuzzedDataProvider.h"

#include "typecd/test_constants.h"

using testing::_;
using testing::ByMove;
using testing::Return;
using testing::StrEq;

namespace {

// Stub observer so that UdevMonitor has some callbacks to use.
class FuzzerObserver : public typecd::UdevMonitor::Observer {
 public:
  void OnPortAddedOrRemoved(const base::FilePath& path,
                            int port_num,
                            bool added) override{};
  void OnPartnerAddedOrRemoved(const base::FilePath& path,
                               int port_num,
                               bool added) override{};
  void OnPartnerAltModeAddedOrRemoved(const base::FilePath& path,
                                      int port_num,
                                      bool added) override{};
  void OnCableAddedOrRemoved(const base::FilePath& path,
                             int port_num,
                             bool added) override{};
  void OnCablePlugAdded(const base::FilePath& path, int port_num) override{};
  void OnCableAltModeAdded(const base::FilePath& path, int port_num) override{};
  void OnPartnerChanged(int port_num) override{};
  void OnPortChanged(int port_num) override{};
};

}  // namespace

namespace typecd {

// Setup/Teardown code adapted from UdevMonitorTest.
class UdevMonitorFuzzer {
 public:
  UdevMonitorFuzzer()
      : task_environment_(
            base::test::TaskEnvironment::MainThreadType::IO,
            base::test::TaskEnvironment::ThreadPoolExecutionMode::ASYNC) {
    observer_ = std::make_unique<FuzzerObserver>();

    monitor_ = std::make_unique<typecd::UdevMonitor>();
    monitor_->AddObserver(observer_.get());
  }

  ~UdevMonitorFuzzer() {
    monitor_.reset();
    observer_.reset();
  }

  void SetUdev(std::unique_ptr<brillo::MockUdev> udev) {
    monitor_->SetUdev(std::move(udev));
  }

  void CallScanDevices() { monitor_->ScanDevices(); }

 private:
  // Add a task environment to keep the FileDescriptorWatcher code happy.
  base::test::TaskEnvironment task_environment_;
  std::unique_ptr<FuzzerObserver> observer_;
  std::unique_ptr<typecd::UdevMonitor> monitor_;
};

}  // namespace typecd

class Environment {
 public:
  Environment() { logging::SetMinLogLevel(logging::LOGGING_ERROR); }
};

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  static Environment env;
  FuzzedDataProvider data_provider(data, size);
  typecd::UdevMonitorFuzzer fuzzer;

  // We should at least have 2 1 character length strings.
  if (size < 2)
    return 0;

  auto entry2_str = data_provider.ConsumeRandomLengthString(size / 2);
  auto list_entry2 = std::make_unique<brillo::MockUdevListEntry>();
  EXPECT_CALL(*list_entry2, GetName()).WillOnce(Return(entry2_str.c_str()));
  EXPECT_CALL(*list_entry2, GetNext()).WillOnce(Return(ByMove(nullptr)));

  auto entry1_str = data_provider.ConsumeRandomLengthString(size / 2);
  auto list_entry1 = std::make_unique<brillo::MockUdevListEntry>();
  EXPECT_CALL(*list_entry1, GetName()).WillOnce(Return(entry1_str.c_str()));
  EXPECT_CALL(*list_entry1, GetNext())
      .WillOnce(Return(ByMove(std::move(list_entry2))));

  // Ensuring that when we add the "typec" subsystem matcher to the udev
  // monitor, we don't fail.
  auto enumerate = std::make_unique<brillo::MockUdevEnumerate>();
  EXPECT_CALL(*enumerate, AddMatchSubsystem(StrEq(typecd::kTypeCSubsystem)))
      .WillOnce(Return(true));
  EXPECT_CALL(*enumerate, ScanDevices()).WillOnce(Return(true));
  EXPECT_CALL(*enumerate, GetListEntry())
      .WillOnce(Return(ByMove(std::move(list_entry1))));

  auto udev = std::make_unique<brillo::MockUdev>();
  EXPECT_CALL(*udev, CreateEnumerate())
      .WillOnce(Return(ByMove(std::move(enumerate))));

  fuzzer.SetUdev(std::move(udev));
  fuzzer.CallScanDevices();

  return 0;
}
