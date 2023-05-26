// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <libevdev/libevdev.h>
#include <memory>
#include <string>
#include <utility>

#include <base/test/test_future.h>
#include <base/files/file_util.h>
#include <base/test/mock_callback.h>
#include <base/test/task_environment.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "diagnostics/base/file_test_utils.h"
#include "diagnostics/cros_healthd/delegate/utils/evdev_utils.h"

namespace diagnostics {
namespace {

using testing::_;
using testing::DoAll;
using testing::InvokeWithoutArgs;
using testing::Pointer;
using testing::Return;
using testing::SaveArg;
using testing::SetArgPointee;
using testing::StrictMock;
using testing::WithArg;

const base::FilePath kDevInputPath = base::FilePath("/dev/input");

// Reads one byte from fd. Return whether the operation is successful.
bool ReadOneByte(int fd) {
  char buffer;
  return base::ReadFromFD(fd, &buffer, sizeof(buffer));
}

// Write one byte to fd. Return whether the operation is successful.
bool WriteOneByte(int fd) {
  return base::WriteFileDescriptor(fd, "x");
}

class MockDelegate : public EvdevUtil::Delegate {
 public:
  MockDelegate() {}
  MockDelegate(const MockDelegate&) = delete;
  MockDelegate& operator=(const MockDelegate&) = delete;

  // EvdevUtil::Delegate overrides.
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

class MockLibevdevWrapper : public LibevdevWrapper {
 public:
  MockLibevdevWrapper() {}
  MockLibevdevWrapper(const MockLibevdevWrapper&) = delete;
  MockLibevdevWrapper& operator=(const MockLibevdevWrapper&) = delete;
  ~MockLibevdevWrapper() = default;

  // LibevdevWrapper overrides:
  MOCK_METHOD(bool, HasProperty, (unsigned int prop), (override));
  MOCK_METHOD(bool, HasEventType, (unsigned int type), (override));
  MOCK_METHOD(bool,
              HasEventCode,
              (unsigned int type, unsigned int code),
              (override));
  // No need to mock |GetName| since it's only for logging.
  std::string GetName() override { return "Mock device name"; }
  MOCK_METHOD(int, GetIdBustype, (), (override));
  MOCK_METHOD(int, GetAbsMaximum, (unsigned int code), (override));
  MOCK_METHOD(int,
              GetEventValue,
              (unsigned int type, unsigned int code),
              (override));
  MOCK_METHOD(int, GetNumSlots, (), (override));
  MOCK_METHOD(int,
              FetchSlotValue,
              (unsigned int slot, unsigned int code, int* value),
              (override));
  MOCK_METHOD(int,
              NextEvent,
              (unsigned int flags, input_event* ev),
              (override));
};

class EvdevUtilsTest : public ::testing::Test {
 protected:
  EvdevUtilsTest() = default;
  EvdevUtilsTest(const EvdevUtilsTest&) = delete;
  EvdevUtilsTest& operator=(const EvdevUtilsTest&) = delete;

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

  void StartEvdevUtil() {
    CHECK(mock_delegate_);
    CHECK(!evdev_util_) << "StartEvdevUtil can only be called once";
    evdev_util_ = std::make_unique<EvdevUtil>(std::move(mock_delegate_),
                                              mock_factory_method_.Get());
  }

  MockDelegate& mock_delegate() {
    CHECK(mock_delegate_);
    return *mock_delegate_;
  }

  StrictMock<base::MockCallback<EvdevUtil::LibevdevWrapperFactoryMethod>>
      mock_factory_method_;

 private:
  // |MainThreadType::IO| is required by FileDescriptorWatcher.
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::MainThreadType::IO};
  ScopedRootDirOverrides root_overrides_;
  std::unique_ptr<StrictMock<MockDelegate>> mock_delegate_;
  std::unique_ptr<EvdevUtil> evdev_util_;
};

// Create an evdev node, set a fake event and verify the received event.
TEST_F(EvdevUtilsTest, ReceiveEventsSuccessfully) {
  auto scoped_fd = CreateAndOpenFakeEvdevNode("event0");
  ASSERT_TRUE(scoped_fd.is_valid());

  const input_event fake_event{.type = 1, .code = 2, .value = 3};
  auto libevdev_wrapper = std::make_unique<StrictMock<MockLibevdevWrapper>>();
  // Save the pointer to verify the later accesses are against this instance.
  LibevdevWrapper* const libevdev_wrapper_ptr = libevdev_wrapper.get();
  EXPECT_CALL(*libevdev_wrapper, NextEvent)
      .WillOnce(DoAll(SetArgPointee<1>(fake_event),
                      Return(LIBEVDEV_READ_STATUS_SUCCESS)))
      .WillRepeatedly(Return(-EAGAIN));

  EXPECT_CALL(mock_factory_method_, Run)
      .WillOnce(Return(std::move(libevdev_wrapper)));

  base::test::TestFuture<input_event> event_future;
  EXPECT_CALL(mock_delegate(), IsTarget(Pointer(libevdev_wrapper_ptr)))
      .WillOnce(Return(true));
  EXPECT_CALL(mock_delegate(), ReportProperties(Pointer(libevdev_wrapper_ptr)))
      .Times(1);
  EXPECT_CALL(mock_delegate(), FireEvent(_, Pointer(libevdev_wrapper_ptr)))
      .WillOnce(
          DoAll(WithArg<0>([&](auto event) { event_future.SetValue(event); }),
                // Read data to make reading file blocked again.
                InvokeWithoutArgs(
                    [&]() { ASSERT_TRUE(ReadOneByte(scoped_fd.get())); })));

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
      .WillOnce(Return(std::make_unique<MockLibevdevWrapper>()));

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

}  // namespace
}  // namespace diagnostics
