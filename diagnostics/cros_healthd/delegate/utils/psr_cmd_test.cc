// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/delegate/utils/psr_cmd.h"

#include <optional>
#include <vector>

#include <base/strings/string_util.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace diagnostics::psr {

using ::testing::_;
using ::testing::DoAll;
using ::testing::Return;
using ::testing::SetArgReferee;

class MockPsrCmd : public PsrCmd {
  static constexpr char kFd[] = "/dev/mei0";

 public:
  MockPsrCmd() : PsrCmd(kFd) {}
  MockPsrCmd(const MockPsrCmd&) = delete;
  MockPsrCmd& operator=(const MockPsrCmd&) = delete;
  virtual ~MockPsrCmd() = default;

  using PsrCmd::IdToHexString;
  MOCK_METHOD(bool, MeiConnect, (), (override));
  MOCK_METHOD(bool, MeiSend, (void* buffer, ssize_t buff_len), (override));
  MOCK_METHOD(bool,
              MeiReceive,
              (std::vector<uint8_t> & buffer, ssize_t& buff_len),
              (override));
  MOCK_METHOD(CmdStatus,
              Transaction,
              (HeciGetRequest & tx_buff, PsrHeciResp& rx_buff),
              (override));
  MOCK_METHOD(CmdStatus,
              Check,
              (FwCapsRequest & tx_buff, FwCapsResp& rx_buff),
              (override));
  MOCK_METHOD(std::optional<bool>, CheckPlatformServiceRecord, (), (override));
};

// Check IdToHexString.
TEST(PsrCmdTest, IdToHexString) {
  MockPsrCmd cmd;
  const int len = 2;
  uint8_t id[len] = {205, 171};

  EXPECT_EQ(cmd.IdToHexString(id, len), "cdab");  // Success.
  EXPECT_EQ(cmd.IdToHexString(id, 0), "");        // Empty.
}

// Check GetPlatformServiceRecord.
TEST(PsrCmdTest, GetPlatformServiceRecord) {
  MockPsrCmd cmd;
  HeciGetRequest heci_get_request;
  GenesisRecord gr;
  LedgerRecord lr;
  Event evt = {.event_type = EventType::kLogStart};
  PlatformServiceRecord psr = {.uuid[0] = 0xac,
                               .upid[0] = 0xfa,
                               .genesis_info = gr,
                               .ledger_info = lr,
                               .events_count = 1,
                               .events_info[0] = evt};
  PsrHeciResp psr_heci_resp = {.log_state = LogState::kStarted,
                               .psr_record = psr};

  EXPECT_CALL(cmd, Transaction(_, _))
      .WillOnce(DoAll(SetArgReferee<0>(heci_get_request),
                      SetArgReferee<1>(psr_heci_resp),
                      Return(MockPsrCmd::CmdStatus::kSuccess)));

  PsrHeciResp psr_heci_resp_out;
  EXPECT_EQ(cmd.GetPlatformServiceRecord(psr_heci_resp_out), true);
  EXPECT_EQ(psr_heci_resp_out.log_state, psr_heci_resp.log_state);
  EXPECT_EQ(psr_heci_resp_out.psr_record.uuid[0],
            psr_heci_resp.psr_record.uuid[0]);
  EXPECT_EQ(psr_heci_resp_out.psr_record.upid[0],
            psr_heci_resp.psr_record.upid[0]);
  EXPECT_EQ(psr_heci_resp_out.psr_record.events_info[0].event_type,
            psr_heci_resp.psr_record.events_info[0].event_type);
  EXPECT_EQ(psr_heci_resp_out.psr_record.events_count,
            psr_heci_resp.psr_record.events_count);  // Success.
}

}  // namespace diagnostics::psr
