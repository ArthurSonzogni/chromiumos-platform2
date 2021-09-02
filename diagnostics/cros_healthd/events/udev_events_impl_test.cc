// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>
#include <utility>

#include <base/files/file_util.h>
#include <base/observer_list.h>
#include <base/observer_list_types.h>
#include <base/test/task_environment.h>
#include <brillo/udev/mock_udev.h>
#include <brillo/udev/mock_udev_device.h>
#include <brillo/udev/mock_udev_monitor.h>
#include <brillo/unittest_utils.h>
#include <gtest/gtest.h>
#include <gtest/gtest_prod.h>

#include "diagnostics/common/file_test_utils.h"
#include "diagnostics/common/mojo_type_utils.h"
#include "diagnostics/cros_healthd/events/udev_events_impl.h"
#include "diagnostics/cros_healthd/system/mock_context.h"
#include "mojo/cros_healthd_events.mojom.h"

namespace diagnostics {
namespace {

namespace mojo_ipc = ::chromeos::cros_healthd::mojom;

using testing::ByMove;
using testing::Invoke;
using testing::Return;
using testing::StrictMock;

constexpr const char kUdevActionAdd[] = "add";
constexpr const char kUdevActionRemove[] = "remove";
constexpr const char kUdevThunderboltSubSystem[] = "thunderbolt";
constexpr char kFakeThunderboltDevicePath[] =
    "sys/bus/thunderbolt/devices/domain0/";
constexpr const char kUdevActionChange[] = "change";
constexpr char kFakeThunderboltFullPath[] =
    "sys/bus/thunderbolt/devices/domain0/0-0:1-0";
constexpr char kFileThunderboltAuthorized[] = "authorized";
constexpr char kThunderboltAuthorized[] = "1";
constexpr char kThunderboltUnAuthorized[] = "0";

class MockCrosHealthdThunderboltObserver
    : public mojo_ipc::CrosHealthdThunderboltObserver {
 public:
  explicit MockCrosHealthdThunderboltObserver(
      mojo::PendingReceiver<mojo_ipc::CrosHealthdThunderboltObserver> receiver)
      : receiver_{this /* impl */, std::move(receiver)} {
    DCHECK(receiver_.is_bound());
  }
  MockCrosHealthdThunderboltObserver(
      const MockCrosHealthdThunderboltObserver&) = delete;
  MockCrosHealthdThunderboltObserver& operator=(
      const MockCrosHealthdThunderboltObserver&) = delete;

  MOCK_METHOD(void, OnAdd, (), (override));
  MOCK_METHOD(void, OnRemove, (), (override));
  MOCK_METHOD(void, OnAuthorized, (), (override));
  MOCK_METHOD(void, OnUnAuthorized, (), (override));

 private:
  mojo::Receiver<mojo_ipc::CrosHealthdThunderboltObserver> receiver_;
};

}  // namespace

class UdevEventsImplTest : public BaseFileTest {
 public:
  UdevEventsImplTest()
      : task_environment_(
            base::test::TaskEnvironment::MainThreadType::IO,
            base::test::TaskEnvironment::ThreadPoolExecutionMode::ASYNC) {}

  void SetUp() override {
    mojo::PendingRemote<mojo_ipc::CrosHealthdThunderboltObserver> observer;
    mojo::PendingReceiver<mojo_ipc::CrosHealthdThunderboltObserver>
        observer_receiver(observer.InitWithNewPipeAndPassReceiver());
    observer_ =
        std::make_unique<StrictMock<MockCrosHealthdThunderboltObserver>>(
            std::move(observer_receiver));
    udev_events_impl_.AddThunderboltObserver(std::move(observer));
    SetTestRoot(mock_context_.root_dir());
  }

  MockCrosHealthdThunderboltObserver* mock_observer() {
    return observer_.get();
  }

  void DestroyMojoObserver() {
    observer_.reset();
    task_environment_.RunUntilIdle();
  }

  UdevEventsImpl* udev_events_impl() { return &udev_events_impl_; }

  void SetUpSysfsFile(const std::string& val) {
    const auto dir = kFakeThunderboltDevicePath;
    const auto dev_file = "0-0:1-0";
    SetFile({dir, dev_file, kFileThunderboltAuthorized}, val);
  }

  void TriggerUdevEvent(const char* action, const char* authorized) {
    const auto& root = mock_context_.root_dir();
    auto path = root.Append(kFakeThunderboltFullPath);
    auto monitor = mock_context_.mock_udev_monitor();
    auto device = std::make_unique<brillo::MockUdevDevice>();
    EXPECT_CALL(*device, GetAction()).WillOnce(Return(action));
    EXPECT_CALL(*device, GetSubsystem())
        .WillOnce(Return(kUdevThunderboltSubSystem));
    if (authorized) {
      SetUpSysfsFile(std::string(authorized));
      EXPECT_CALL(*device, GetSysPath()).WillOnce(Return(path.value().c_str()));
    }
    EXPECT_CALL(*monitor, ReceiveDevice())
        .WillOnce(Return(ByMove(std::move(device))));
    udev_events_impl()->OnUdevEvent();
  }

 protected:
  base::test::TaskEnvironment task_environment_;
  MockContext mock_context_;
  std::unique_ptr<brillo::MockUdevDevice> device_;
  std::unique_ptr<brillo::UdevMonitor> monitor_;
  UdevEventsImpl udev_events_impl_{&mock_context_};
  std::unique_ptr<StrictMock<MockCrosHealthdThunderboltObserver>> observer_;
};

TEST_F(UdevEventsImplTest, TestThunderboltAddEvent) {
  base::RunLoop run_loop;
  EXPECT_CALL(*mock_observer(), OnAdd()).WillOnce(Invoke([&]() {
    run_loop.Quit();
  }));

  TriggerUdevEvent(kUdevActionAdd, nullptr);

  run_loop.Run();
}

TEST_F(UdevEventsImplTest, TestThunderboltRemoveEvent) {
  base::RunLoop run_loop;
  EXPECT_CALL(*mock_observer(), OnRemove()).WillOnce(Invoke([&]() {
    run_loop.Quit();
  }));

  TriggerUdevEvent(kUdevActionRemove, nullptr);

  run_loop.Run();
}

TEST_F(UdevEventsImplTest, TestThunderboltAuthorizedEvent) {
  base::RunLoop run_loop;
  EXPECT_CALL(*mock_observer(), OnAuthorized()).WillOnce(Invoke([&]() {
    run_loop.Quit();
  }));

  TriggerUdevEvent(kUdevActionChange, kThunderboltAuthorized);

  run_loop.Run();
}

TEST_F(UdevEventsImplTest, TestThunderboltUnAuthorizedEvent) {
  base::RunLoop run_loop;
  EXPECT_CALL(*mock_observer(), OnUnAuthorized()).WillOnce(Invoke([&]() {
    run_loop.Quit();
  }));

  TriggerUdevEvent(kUdevActionChange, kThunderboltUnAuthorized);

  run_loop.Run();
}

}  // namespace diagnostics
