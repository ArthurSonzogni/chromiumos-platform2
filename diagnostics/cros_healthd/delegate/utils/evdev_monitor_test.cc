// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/delegate/utils/evdev_monitor.h"

#include <fcntl.h>
#include <linux/input-event-codes.h>

#include <memory>
#include <string>
#include <utility>

#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/stringprintf.h>
#include <base/test/mock_callback.h>
#include <base/test/repeating_test_future.h>
#include <base/test/task_environment.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <libevdev/libevdev.h>

#include "diagnostics/base/file_utils.h"
#include "diagnostics/cros_healthd/delegate/utils/test/mock_libevdev_wrapper.h"

namespace diagnostics {
namespace {

using ::testing::_;
using ::testing::DoAll;
using ::testing::InvokeWithoutArgs;
using ::testing::Pointer;
using ::testing::Return;
using ::testing::SetArgPointee;
using ::testing::StrictMock;
using ::testing::WithArg;

const base::FilePath kDevInputPath = base::FilePath("/dev/input");

// Reads one byte from fd. Return whether the operation is successful.
bool ReadOneByte(int fd) {
  char buffer;
  return base::ReadFromFD(fd, base::span_from_ref(buffer));
}

// Write one byte to fd. Return whether the operation is successful.
bool WriteOneByte(int fd) {
  return base::WriteFileDescriptor(fd, "x");
}

class MockDelegate : public EvdevMonitor::Delegate {
 public:
  MockDelegate() = default;
  MockDelegate(const MockDelegate&) = delete;
  MockDelegate& operator=(const MockDelegate&) = delete;

  // EvdevMonitor::Delegate overrides.
  MOCK_METHOD(bool, IsTarget, (LibevdevWrapper * dev), (override));
  MOCK_METHOD(void,
              FireEvent,
              (const input_event& event, LibevdevWrapper* dev),
              (override));
  MOCK_METHOD(void,
              InitializationFail,
              (uint32_t custom_reason, const std::string& description),
              (override));
  MOCK_METHOD(void, ReportProperties, (LibevdevWrapper * dev), (override));
};

class FakeEvdevUtil : public EvdevMonitor {
 public:
  using LibevdevWrapperFactoryMethod =
      base::RepeatingCallback<std::unique_ptr<LibevdevWrapper>(int fd)>;
  FakeEvdevUtil(std::unique_ptr<Delegate> delegate,
                LibevdevWrapperFactoryMethod factory_method)
      : EvdevMonitor(std::move(delegate)),
        factory_method_(std::move(factory_method)) {}
  FakeEvdevUtil(const FakeEvdevUtil& oth) = delete;
  FakeEvdevUtil(FakeEvdevUtil&& oth) = delete;
  ~FakeEvdevUtil() = default;

  std::unique_ptr<LibevdevWrapper> CreateLibevdev(int fd) override {
    return factory_method_.Run(fd);
  }

 private:
  LibevdevWrapperFactoryMethod factory_method_;
};

class EvdevUtilsTest : public ::testing::Test {
 public:
  EvdevUtilsTest(const EvdevUtilsTest&) = delete;
  EvdevUtilsTest& operator=(const EvdevUtilsTest&) = delete;

 protected:
  EvdevUtilsTest() = default;

  void SetUp() override {
    ASSERT_TRUE(base::CreateDirectory(GetRootedPath(kDevInputPath)));
    mock_delegate_ = std::make_unique<StrictMock<MockDelegate>>();
  }

  base::ScopedFD CreateAndOpenFakeEvdevNode(std::string node_name) {
    auto path = GetRootedPath(kDevInputPath.Append(node_name));
    int res = mkfifo(path.value().c_str(), 0644);
    if (res != 0) {
      LOG(ERROR) << "mkfifo failed, return value = " << res;
      return base::ScopedFD();
    }
    return base::ScopedFD(open(path.value().c_str(), O_RDWR));
  }

  void StartEvdevUtil(bool allow_multiple_devices = false) {
    CHECK(mock_delegate_);
    CHECK(!evdev_util_) << "StartEvdevUtil can only be called once";
    evdev_util_ = std::make_unique<FakeEvdevUtil>(std::move(mock_delegate_),
                                                  mock_factory_method_.Get());
    evdev_util_->StartMonitoring(allow_multiple_devices);
  }

  void ExpectEventFromNode(int fd, input_event fake_event) {
    auto libevdev_wrapper =
        std::make_unique<StrictMock<test::MockLibevdevWrapper>>();
    // Save the pointer to verify the later accesses are against this instance.
    LibevdevWrapper* const libevdev_wrapper_ptr = libevdev_wrapper.get();
    EXPECT_CALL(*libevdev_wrapper, NextEvent)
        .WillOnce(DoAll(SetArgPointee<1>(fake_event),
                        Return(LIBEVDEV_READ_STATUS_SUCCESS)))
        .WillRepeatedly(Return(-EAGAIN));

    EXPECT_CALL(mock_factory_method_, Run)
        .WillOnce(Return(std::move(libevdev_wrapper)))
        .RetiresOnSaturation();

    EXPECT_CALL(mock_delegate(), IsTarget(Pointer(libevdev_wrapper_ptr)))
        .WillOnce(Return(true));
    EXPECT_CALL(mock_delegate(),
                ReportProperties(Pointer(libevdev_wrapper_ptr)))
        .Times(1);
    EXPECT_CALL(mock_delegate(), FireEvent(_, Pointer(libevdev_wrapper_ptr)))
        .WillOnce(
            DoAll(WithArg<0>([&](auto event) { event_future.AddValue(event); }),
                  // Read data to make reading file blocked again.
                  InvokeWithoutArgs([=]() { ASSERT_TRUE(ReadOneByte(fd)); })));
  }

  MockDelegate& mock_delegate() {
    CHECK(mock_delegate_);
    return *mock_delegate_;
  }

  StrictMock<base::MockCallback<
      base::RepeatingCallback<std::unique_ptr<LibevdevWrapper>(int)>>>
      mock_factory_method_;
  base::test::RepeatingTestFuture<input_event> event_future;

 private:
  // |MainThreadType::IO| is required by FileDescriptorWatcher.
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::MainThreadType::IO};
  ScopedRootDirOverrides root_overrides_;
  std::unique_ptr<StrictMock<MockDelegate>> mock_delegate_;
  std::unique_ptr<EvdevMonitor> evdev_util_;
};

// Create an evdev node, set a fake event and verify the received event.
TEST_F(EvdevUtilsTest, ReceiveEventsSuccessfully) {
  auto scoped_fd = CreateAndOpenFakeEvdevNode("event0");
  ASSERT_TRUE(scoped_fd.is_valid());

  const input_event fake_event{.type = 1, .code = 2, .value = 3};
  ExpectEventFromNode(scoped_fd.get(), fake_event);

  StartEvdevUtil();

  // Write data to make the file readable without blocking.
  ASSERT_TRUE(WriteOneByte(scoped_fd.get()));

  auto received_event = event_future.Take();
  EXPECT_EQ(received_event.type, 1);
  EXPECT_EQ(received_event.code, 2);
  EXPECT_EQ(received_event.value, 3);
}

TEST_F(EvdevUtilsTest, InitializationFailIfNoEvdevNodes) {
  EXPECT_CALL(mock_delegate(), InitializationFail).Times(1);

  StartEvdevUtil();
}

TEST_F(EvdevUtilsTest, InitializationFailIfNoTargetDevices) {
  auto scoped_fd = CreateAndOpenFakeEvdevNode("event0");
  ASSERT_TRUE(scoped_fd.is_valid());

  EXPECT_CALL(mock_factory_method_, Run)
      .WillOnce(Return(std::make_unique<test::MockLibevdevWrapper>()));

  EXPECT_CALL(mock_delegate(), IsTarget).WillOnce(Return(false));
  EXPECT_CALL(mock_delegate(), InitializationFail).Times(1);

  StartEvdevUtil();
}

TEST_F(EvdevUtilsTest, InitializationFailIfLibevdevCreationFailed) {
  auto scoped_fd = CreateAndOpenFakeEvdevNode("event0");
  ASSERT_TRUE(scoped_fd.is_valid());

  EXPECT_CALL(mock_factory_method_, Run).WillOnce(Return(nullptr));

  EXPECT_CALL(mock_delegate(), InitializationFail).Times(1);

  StartEvdevUtil();
}

class EvdevUtilsAllowMultipleDeviceTest
    : public EvdevUtilsTest,
      public ::testing::WithParamInterface<int> {
 protected:
  int evdev_node_count() { return GetParam(); }
};

// Create evdev nodes, set fake events and verify the received events.
TEST_P(EvdevUtilsAllowMultipleDeviceTest, ReceiveEventsSuccessfully) {
  std::vector<base::ScopedFD> fds;

  for (int i = 0; i < evdev_node_count(); ++i) {
    const auto event_file_name = base::StringPrintf("event%d", i);
    auto scoped_fd = CreateAndOpenFakeEvdevNode(event_file_name);
    ASSERT_TRUE(scoped_fd.is_valid());

    const input_event fake_event{.type = 1, .code = 2, .value = 3};
    ExpectEventFromNode(scoped_fd.get(), fake_event);
    fds.push_back(std::move(scoped_fd));
  }

  StartEvdevUtil(/*allow_multiple_devices*/ true);

  // Write data to make the file readable without blocking.
  for (int i = 0; i < evdev_node_count(); ++i) {
    ASSERT_TRUE(WriteOneByte(fds[i].get()));
  }
  for (int i = 0; i < evdev_node_count(); ++i) {
    auto received_event = event_future.Take();
    EXPECT_EQ(received_event.type, 1);
    EXPECT_EQ(received_event.code, 2);
    EXPECT_EQ(received_event.value, 3);
  }
}

INSTANTIATE_TEST_SUITE_P(DifferentNumberOfEvdevNodes,
                         EvdevUtilsAllowMultipleDeviceTest,
                         testing::Values(1, 2, 3));

}  // namespace
}  // namespace diagnostics
