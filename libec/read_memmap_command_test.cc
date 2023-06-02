// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <cstdint>

#include "libec/read_memmap_command.h"

using testing::_;
using testing::InvokeWithoutArgs;
using testing::Return;

namespace ec {

namespace {

template <typename Output>
class MockReadMemmapCommand : public ReadMemmapCommand<Output> {
 public:
  using ReadMemmapCommand<Output>::ReadMemmapCommand;
  explicit MockReadMemmapCommand(uint8_t offset)
      : ReadMemmapCommand<Output>(offset) {}
  ~MockReadMemmapCommand() override = default;

  MOCK_METHOD(int,
              IoctlReadmem,
              (int fd, uint32_t request, cros_ec_readmem_v2* data),
              (override));

  MOCK_METHOD(bool, EcCommandRun, (int fd), (override));
};

}  // namespace

TEST(EcCommand, RunWithIoctl) {
  // Choose a random type for testing the template command.
  using Type = uint32_t;
  constexpr uint8_t offset = 10;
  Type ret = 100;
  MockReadMemmapCommand<uint8_t> mock{offset};
  EXPECT_CALL(mock, IoctlReadmem)
      .WillOnce([ret](int fd, uint32_t offset, cros_ec_readmem_v2* data) {
        data->offset = offset;
        data->bytes = sizeof(Type);
        std::memcpy(data->buffer, &ret, sizeof(Type));
        return sizeof(Type);
      });
  EXPECT_TRUE(mock.Run(offset));
  EXPECT_EQ(*mock.Resp(), ret);
}

TEST(EcCommand, RunFallbackToEcCommand) {
  // Choose a random type for testing the template command.
  constexpr uint8_t offset = 10;
  MockReadMemmapCommand<uint8_t> mock{offset};
  EXPECT_CALL(mock, IoctlReadmem)
      .WillOnce(
          [](int fd, uint32_t offset, cros_ec_readmem_v2* data) { return -1; });
  EXPECT_CALL(mock, EcCommandRun).WillOnce([](int fd) { return true; });
  EXPECT_TRUE(mock.Run(offset));
}

TEST(EcCommand, FailedRun) {
  // Choose a random type for testing the template command.
  constexpr uint8_t offset = 10;
  MockReadMemmapCommand<uint8_t> mock{offset};
  EXPECT_CALL(mock, IoctlReadmem)
      .WillOnce(
          [](int fd, uint32_t offset, cros_ec_readmem_v2* data) { return -1; });
  EXPECT_CALL(mock, EcCommandRun).WillOnce([](int fd) { return false; });
  EXPECT_FALSE(mock.Run(offset));
}

}  // namespace ec
