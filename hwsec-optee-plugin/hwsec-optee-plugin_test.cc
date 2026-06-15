// Copyright 2026 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hwsec-optee-plugin/hwsec-optee-plugin.h"

#include <cstdint>
#include <vector>

#include <base/sys_byteorder.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <libhwsec/frontend/optee-plugin/mock_frontend.h>

extern "C" {
#include <tee_plugin_method.h>
}

using ::testing::_;
using ::testing::Return;

namespace hwsec {
namespace {

constexpr uint32_t kSendRawCommand = 0;

constexpr uint32_t kTpmCcStartAuthSession = 0x00000176;
constexpr uint32_t kTpmCcFlushContext = 0x00000165;
constexpr uint32_t kTpmCcNvReadPublic = 0x00000169;
constexpr uint32_t kTpmCcNvRead = 0x0000014E;
constexpr uint32_t kTpmCcNvIncrement = 0x00000134;
constexpr uint32_t kTpmCcPcrExtend = 0x00000182;  // Disallowed

class HwsecOpteePluginTest : public ::testing::Test {
 protected:
  void SetUp() override { SetHwsecForTesting(&mock_frontend_); }

  void TearDown() override { SetHwsecForTesting(nullptr); }

  std::vector<uint8_t> CreateTpmCommand(uint32_t command_code,
                                        uint32_t size = 10) {
    std::vector<uint8_t> buf(size);
    // tag (big endian) - TPM_ST_NO_SESSIONS
    buf[0] = 0x80;
    buf[1] = 0x01;
    // size (big endian)
    uint32_t size_be = base::HostToNet32(size);
    memcpy(buf.data() + 2, &size_be, 4);
    // command code (big endian)
    uint32_t cc_be = base::HostToNet32(command_code);
    memcpy(buf.data() + 6, &cc_be, 4);
    return buf;
  }

  MockOpteePluginFrontend mock_frontend_;
};

TEST_F(HwsecOpteePluginTest, AllowedCommands) {
  uint32_t allowed_ccs[] = {
      kTpmCcStartAuthSession, kTpmCcFlushContext, kTpmCcNvReadPublic,
      kTpmCcNvRead,           kTpmCcNvIncrement,
  };

  for (uint32_t cc : allowed_ccs) {
    std::vector<uint8_t> cmd = CreateTpmCommand(cc);
    size_t out_len = cmd.size();

    EXPECT_CALL(mock_frontend_, SendRawCommand(_))
        .WillOnce(Return(brillo::Blob({1, 2, 3})));  // Dummy response

    TEEC_Result res = plugin_method.invoke(kSendRawCommand, 0, cmd.data(),
                                           cmd.size(), &out_len);
    EXPECT_EQ(res, TEEC_SUCCESS) << "Failed for CC 0x" << std::hex << cc;
  }
}

TEST_F(HwsecOpteePluginTest, DisallowedCommand) {
  std::vector<uint8_t> cmd = CreateTpmCommand(kTpmCcPcrExtend);
  size_t out_len = cmd.size();

  EXPECT_CALL(mock_frontend_, SendRawCommand(_)).Times(0);

  TEEC_Result res = plugin_method.invoke(kSendRawCommand, 0, cmd.data(),
                                         cmd.size(), &out_len);
  EXPECT_EQ(res, TEEC_ERROR_ACCESS_DENIED);
}

TEST_F(HwsecOpteePluginTest, TooShortInput) {
  std::vector<uint8_t> cmd(5, 0);  // Too short for header (10 bytes)
  size_t out_len = cmd.size();

  EXPECT_CALL(mock_frontend_, SendRawCommand(_)).Times(0);

  TEEC_Result res = plugin_method.invoke(kSendRawCommand, 0, cmd.data(),
                                         cmd.size(), &out_len);
  EXPECT_EQ(res, TEEC_ERROR_BAD_PARAMETERS);
}

TEST_F(HwsecOpteePluginTest, SizeMismatch) {
  std::vector<uint8_t> cmd = CreateTpmCommand(kTpmCcStartAuthSession, 10);
  // Modify header size to be larger than data_len
  uint32_t invalid_size = 20;
  uint32_t size_be = base::HostToNet32(invalid_size);
  memcpy(cmd.data() + 2, &size_be, 4);

  size_t out_len = cmd.size();

  EXPECT_CALL(mock_frontend_, SendRawCommand(_)).Times(0);

  TEEC_Result res = plugin_method.invoke(kSendRawCommand, 0, cmd.data(),
                                         cmd.size(), &out_len);
  EXPECT_EQ(res, TEEC_ERROR_BAD_PARAMETERS);
}

}  // namespace
}  // namespace hwsec
