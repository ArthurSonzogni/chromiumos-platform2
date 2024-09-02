// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/delegate/fetchers/psr_fetcher.h"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include <base/strings/safe_sprintf.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "diagnostics/base/file_test_utils.h"
#include "diagnostics/base/paths.h"
#include "diagnostics/cros_healthd/delegate/utils/psr_cmd.h"
#include "diagnostics/mojom/public/cros_healthd_probe.mojom.h"

namespace diagnostics {
namespace {

namespace mojom = ::ash::cros_healthd::mojom;

using ::testing::_;
using ::testing::DoAll;
using ::testing::Return;
using ::testing::SetArgReferee;
using ::testing::SizeIs;

class MockPsrCmd : public psr::PsrCmdVirt {
 public:
  MOCK_METHOD(bool, GetPlatformServiceRecord, (psr::PsrHeciResp&), (override));
  MOCK_METHOD(std::optional<bool>, CheckPlatformServiceRecord, (), (override));
  MOCK_METHOD(std::string,
              IdToHexString,
              (uint8_t id[], int id_len),
              (override));
};

class MockPsrFetcher : public PsrFetcher {
 public:
  MOCK_METHOD(std::unique_ptr<psr::PsrCmdVirt>, CreatePsrCmd, (), (override));
};

class PsrFetcherTest : public BaseFileTest {
 public:
  PsrFetcherTest(const PsrFetcherTest&) = delete;
  PsrFetcherTest& operator=(const PsrFetcherTest&) = delete;

 protected:
  PsrFetcherTest() = default;
  ~PsrFetcherTest() = default;

  void SetUp() override { SetFile(paths::dev::kMei0, ""); }

  MockPsrFetcher& mock_psr_fetcher() { return psr_fetcher_; }

 private:
  MockPsrFetcher psr_fetcher_;
};

TEST_F(PsrFetcherTest, ConvertLogStateToMojo) {
  EXPECT_EQ(internal::ConvertLogStateToMojo(psr::LogState::kNotStarted),
            mojom::PsrInfo::LogState::kNotStarted);
  EXPECT_EQ(internal::ConvertLogStateToMojo(psr::LogState::kStarted),
            mojom::PsrInfo::LogState::kStarted);
  EXPECT_EQ(internal::ConvertLogStateToMojo(psr::LogState::kStopped),
            mojom::PsrInfo::LogState::kStopped);
}

TEST_F(PsrFetcherTest, ConvertPsrEventTypeToMojo) {
  EXPECT_EQ(internal::ConvertPsrEventTypeToMojo(psr::EventType::kLogStart),
            mojom::PsrEvent::EventType::kLogStart);
  EXPECT_EQ(internal::ConvertPsrEventTypeToMojo(psr::EventType::kLogEnd),
            mojom::PsrEvent::EventType::kLogEnd);
  EXPECT_EQ(internal::ConvertPsrEventTypeToMojo(psr::EventType::kMissing),
            mojom::PsrEvent::EventType::kMissing);
  EXPECT_EQ(internal::ConvertPsrEventTypeToMojo(psr::EventType::kInvalid),
            mojom::PsrEvent::EventType::kInvalid);
  EXPECT_EQ(internal::ConvertPsrEventTypeToMojo(psr::EventType::kPrtcFailure),
            mojom::PsrEvent::EventType::kPrtcFailure);
  EXPECT_EQ(internal::ConvertPsrEventTypeToMojo(psr::EventType::kCsmeRecovery),
            mojom::PsrEvent::EventType::kCsmeRecovery);
  EXPECT_EQ(internal::ConvertPsrEventTypeToMojo(psr::EventType::kCsmeDamState),
            mojom::PsrEvent::EventType::kCsmeDamState);
  EXPECT_EQ(
      internal::ConvertPsrEventTypeToMojo(psr::EventType::kCsmeUnlockState),
      mojom::PsrEvent::EventType::kCsmeUnlockState);
  EXPECT_EQ(internal::ConvertPsrEventTypeToMojo(psr::EventType::kSvnIncrease),
            mojom::PsrEvent::EventType::kSvnIncrease);
  EXPECT_EQ(
      internal::ConvertPsrEventTypeToMojo(psr::EventType::kFwVersionChanged),
      mojom::PsrEvent::EventType::kFwVersionChanged);
}

TEST_F(PsrFetcherTest, NotSupportedIfDeviceNotFound) {
  UnsetPath(paths::dev::kMei0);

  mojom::GetPsrResultPtr result = mock_psr_fetcher().FetchPsrInfo();
  ASSERT_TRUE(result);
  ASSERT_TRUE(result->is_info());
  const auto& info = result->get_info();
  ASSERT_TRUE(info);
  EXPECT_FALSE(info->is_supported);
}

TEST_F(PsrFetcherTest, ErrorIfFailToCreatePsrCmd) {
  EXPECT_CALL(mock_psr_fetcher(), CreatePsrCmd()).WillOnce(Return(nullptr));

  mojom::GetPsrResultPtr result = mock_psr_fetcher().FetchPsrInfo();
  ASSERT_TRUE(result);
  ASSERT_TRUE(result->is_error());
  EXPECT_EQ(result->get_error(), "Failed to create PsrCmd.");
}

TEST_F(PsrFetcherTest, ErrorIfCheckPsrFailed) {
  auto cmd = std::make_unique<MockPsrCmd>();
  EXPECT_CALL(*cmd, CheckPlatformServiceRecord())
      .WillOnce(Return(std::nullopt));
  EXPECT_CALL(mock_psr_fetcher(), CreatePsrCmd())
      .WillOnce(Return(std::move(cmd)));

  mojom::GetPsrResultPtr result = mock_psr_fetcher().FetchPsrInfo();
  ASSERT_TRUE(result);
  ASSERT_TRUE(result->is_error());
  EXPECT_EQ(result->get_error(), "Check PSR is not working.");
}

TEST_F(PsrFetcherTest, NotSupportedFromFW) {
  auto cmd = std::make_unique<MockPsrCmd>();
  EXPECT_CALL(*cmd, CheckPlatformServiceRecord()).WillOnce(Return(false));
  EXPECT_CALL(mock_psr_fetcher(), CreatePsrCmd())
      .WillOnce(Return(std::move(cmd)));

  mojom::GetPsrResultPtr result = mock_psr_fetcher().FetchPsrInfo();
  ASSERT_TRUE(result);
  ASSERT_TRUE(result->is_info());
  const auto& info = result->get_info();
  ASSERT_TRUE(info);
  EXPECT_FALSE(info->is_supported);
}

TEST_F(PsrFetcherTest, ErrorIfFailedToGetPsr) {
  auto cmd = std::make_unique<MockPsrCmd>();
  EXPECT_CALL(*cmd, CheckPlatformServiceRecord()).WillOnce(Return(true));
  EXPECT_CALL(*cmd, GetPlatformServiceRecord(_)).WillOnce(Return(false));
  EXPECT_CALL(mock_psr_fetcher(), CreatePsrCmd())
      .WillOnce(Return(std::move(cmd)));

  mojom::GetPsrResultPtr result = mock_psr_fetcher().FetchPsrInfo();
  ASSERT_TRUE(result);
  ASSERT_TRUE(result->is_error());
  EXPECT_EQ(result->get_error(), "Get PSR is not working.");
}

TEST_F(PsrFetcherTest, ErrorForUnsupportedVersion) {
  psr::PsrHeciResp psr_res;
  psr_res.psr_version.major = 1;
  psr_res.psr_version.minor = 0;

  auto cmd = std::make_unique<MockPsrCmd>();
  EXPECT_CALL(*cmd, CheckPlatformServiceRecord()).WillOnce(Return(true));
  EXPECT_CALL(*cmd, GetPlatformServiceRecord(_))
      .WillOnce(DoAll(SetArgReferee<0>(psr_res), Return(true)));
  EXPECT_CALL(mock_psr_fetcher(), CreatePsrCmd())
      .WillOnce(Return(std::move(cmd)));

  mojom::GetPsrResultPtr result = mock_psr_fetcher().FetchPsrInfo();
  ASSERT_TRUE(result);
  ASSERT_TRUE(result->is_error());
  EXPECT_EQ(result->get_error(), "Requires PSR 2.0 version.");
}

TEST_F(PsrFetcherTest, Success) {
  psr::PsrHeciResp psr_res;
  psr_res.psr_version.major = 2;
  psr_res.psr_version.minor = 0;
  psr_res.log_state = psr::LogState::kStarted;
  psr_res.psr_record.genesis_info.genesis_date = 42;
  base::strings::SafeSNPrintf(
      reinterpret_cast<char*>(psr_res.psr_record.genesis_info.oem_info),
      psr::kGenesisFieldInfoSize, "OEM-name");
  base::strings::SafeSNPrintf(
      reinterpret_cast<char*>(psr_res.psr_record.genesis_info.oem_make_info),
      psr::kGenesisFieldInfoSize, "OEM-make");
  base::strings::SafeSNPrintf(
      reinterpret_cast<char*>(psr_res.psr_record.genesis_info.oem_model_info),
      psr::kGenesisFieldInfoSize, "OEM-model");
  base::strings::SafeSNPrintf(
      reinterpret_cast<char*>(
          psr_res.psr_record.genesis_info.manufacture_country),
      psr::kGenesisFieldInfoSize, "country");
  base::strings::SafeSNPrintf(
      reinterpret_cast<char*>(psr_res.psr_record.genesis_info.oem_data),
      psr::kGenesisDataStoreInfoSize, "OEM-data");
  psr_res.psr_record.ledger_info
      .ledger_counter[psr::LedgerCounterIndex::kS0Seconds] = 40;
  psr_res.psr_record.ledger_info
      .ledger_counter[psr::LedgerCounterIndex::kS0ToS5] = 45;
  psr_res.psr_record.ledger_info
      .ledger_counter[psr::LedgerCounterIndex::kS0ToS4] = 44;
  psr_res.psr_record.ledger_info
      .ledger_counter[psr::LedgerCounterIndex::kS0ToS3] = 43;
  psr_res.psr_record.ledger_info
      .ledger_counter[psr::LedgerCounterIndex::kWarmReset] = 41;
  psr_res.psr_record.events_count = 2;
  psr_res.psr_record.events_info[0] = psr::Event{
      .event_type = psr::EventType::kLogStart, .timestamp = 100, .data = 10};
  psr_res.psr_record.events_info[1] = psr::Event{
      .event_type = psr::EventType::kLogEnd, .timestamp = 200, .data = 20};

  auto cmd = std::make_unique<MockPsrCmd>();
  EXPECT_CALL(*cmd, CheckPlatformServiceRecord()).WillOnce(Return(true));
  EXPECT_CALL(*cmd, GetPlatformServiceRecord(_))
      .WillOnce(DoAll(SetArgReferee<0>(psr_res), Return(true)));
  EXPECT_CALL(*cmd, IdToHexString(_, psr::kUuidLength))
      .WillOnce(Return("abcd"));
  EXPECT_CALL(*cmd, IdToHexString(_, psr::kUpidLength))
      .WillOnce(Return("01234567"));
  EXPECT_CALL(mock_psr_fetcher(), CreatePsrCmd())
      .WillOnce(Return(std::move(cmd)));

  mojom::GetPsrResultPtr result = mock_psr_fetcher().FetchPsrInfo();
  ASSERT_TRUE(result);
  ASSERT_TRUE(result->is_info());
  const auto& info = result->get_info();
  ASSERT_TRUE(info);
  EXPECT_TRUE(info->is_supported);
  EXPECT_EQ(info->log_state, mojom::PsrInfo::LogState::kStarted);
  EXPECT_EQ(info->uuid, "abcd");
  EXPECT_EQ(info->upid, "01234567");
  EXPECT_EQ(info->log_start_date, 42);
  EXPECT_EQ(info->oem_name, "OEM-name");
  EXPECT_EQ(info->oem_make, "OEM-make");
  EXPECT_EQ(info->oem_model, "OEM-model");
  EXPECT_EQ(info->manufacture_country, "country");
  EXPECT_EQ(info->oem_data, "OEM-data");
  EXPECT_EQ(info->uptime_seconds, 40);
  EXPECT_EQ(info->s5_counter, 45);
  EXPECT_EQ(info->s4_counter, 44);
  EXPECT_EQ(info->s3_counter, 43);
  EXPECT_EQ(info->warm_reset_counter, 41);

  const auto& events = info->events;
  ASSERT_THAT(events, SizeIs(2));
  const auto& event0 = events[0];
  ASSERT_TRUE(event0);
  EXPECT_EQ(event0->type, mojom::PsrEvent::EventType::kLogStart);
  EXPECT_EQ(event0->time, 100);
  EXPECT_EQ(event0->data, 10);
  const auto& event1 = events[1];
  ASSERT_TRUE(event1);
  EXPECT_EQ(event1->type, mojom::PsrEvent::EventType::kLogEnd);
  EXPECT_EQ(event1->time, 200);
  EXPECT_EQ(event1->data, 20);
}

}  // namespace
}  // namespace diagnostics
