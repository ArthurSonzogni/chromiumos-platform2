// Copyright 2012 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "update_engine/cros/payload_state.h"

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/strings/stringprintf.h>
#include <base/test/mock_log.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "update_engine/common/constants.h"
#include "update_engine/common/excluder_interface.h"
#include "update_engine/common/fake_hardware.h"
#include "update_engine/common/metrics_reporter_interface.h"
#include "update_engine/common/mock_excluder.h"
#include "update_engine/common/mock_prefs.h"
#include "update_engine/common/prefs.h"
#include "update_engine/common/test_utils.h"
#include "update_engine/common/utils.h"
#include "update_engine/cros/fake_system_state.h"
#include "update_engine/cros/omaha_request_action.h"

using base::Time;
using base::TimeDelta;
using std::string;
using testing::_;
using testing::AnyNumber;
using testing::AtLeast;
using testing::DoAll;
using testing::HasSubstr;
using testing::Mock;
using testing::NiceMock;
using testing::Return;
using testing::SetArgPointee;
using testing::StrictMock;

namespace chromeos_update_engine {

const char* kCurrentBytesDownloadedFromHttps =
    "current-bytes-downloaded-from-HttpsServer";
const char* kTotalBytesDownloadedFromHttps =
    "total-bytes-downloaded-from-HttpsServer";
const char* kCurrentBytesDownloadedFromHttp =
    "current-bytes-downloaded-from-HttpServer";
const char* kTotalBytesDownloadedFromHttp =
    "total-bytes-downloaded-from-HttpServer";
const char* kCurrentBytesDownloadedFromHttpPeer =
    "current-bytes-downloaded-from-HttpPeer";
const char* kTotalBytesDownloadedFromHttpPeer =
    "total-bytes-downloaded-from-HttpPeer";

static void SetupPayloadStateWith2Urls(string hash,
                                       bool http_enabled,
                                       bool is_delta_payload,
                                       PayloadState* payload_state,
                                       OmahaResponse* response) {
  response->packages.clear();
  response->packages.push_back({.payload_urls = {"http://test", "https://test"},
                                .size = 523456789,
                                .metadata_size = 558123,
                                .metadata_signature = "metasign",
                                .hash = hash,
                                .is_delta = is_delta_payload});
  response->max_failure_count_per_url = 3;
  payload_state->SetResponse(*response);
  string stored_response_sign = payload_state->GetResponseSignature();

  string expected_url_https_only =
      "  NumURLs = 1\n"
      "  Candidate Url0 = https://test\n";

  string expected_urls_both =
      "  NumURLs = 2\n"
      "  Candidate Url0 = http://test\n"
      "  Candidate Url1 = https://test\n";

  string expected_response_sign = base::StringPrintf(
      "Payload 0:\n"
      "  Size = 523456789\n"
      "  Sha256 Hash = %s\n"
      "  Metadata Size = 558123\n"
      "  Metadata Signature = metasign\n"
      "  Is Delta = %d\n"
      "%s"
      "Max Failure Count Per Url = %d\n"
      "Disable Payload Backoff = %d\n",
      hash.c_str(), response->packages[0].is_delta,
      (http_enabled ? expected_urls_both : expected_url_https_only).c_str(),
      response->max_failure_count_per_url, response->disable_payload_backoff);
  EXPECT_EQ(expected_response_sign, stored_response_sign);
}

class PayloadStateTest : public ::testing::Test {
 public:
  void SetUp() { FakeSystemState::CreateInstance(); }

  // TODO(b/171829801): Replace all the |MockPrefs| in this file with
  // |FakePrefs| so we don't have to catch every single unimportant mock call.
};

TEST_F(PayloadStateTest, SetResponseWorksWithEmptyResponse) {
  OmahaResponse response;
  FakeSystemState::Get()->set_prefs(nullptr);
  auto* prefs = FakeSystemState::Get()->mock_prefs();
  EXPECT_CALL(*prefs, SetInt64(_, _)).Times(AnyNumber());
  EXPECT_CALL(*prefs, SetInt64(kPrefsPayloadAttemptNumber, 0))
      .Times(AtLeast(1));
  EXPECT_CALL(*prefs, SetInt64(kPrefsFullPayloadAttemptNumber, 0))
      .Times(AtLeast(1));
  EXPECT_CALL(*prefs, SetInt64(kPrefsBackoffExpiryTime, 0)).Times(AtLeast(1));
  EXPECT_CALL(*prefs, SetInt64(kPrefsCurrentUrlIndex, 0)).Times(AtLeast(1));
  EXPECT_CALL(*prefs, SetInt64(kPrefsCurrentUrlFailureCount, 0))
      .Times(AtLeast(1));
  EXPECT_CALL(*prefs, SetInt64(kPrefsUpdateTimestampStart, _))
      .Times(AtLeast(1));
  EXPECT_CALL(*prefs, SetInt64(kPrefsUpdateDurationUptime, _))
      .Times(AtLeast(1));
  EXPECT_CALL(*prefs, SetInt64(kCurrentBytesDownloadedFromHttps, 0))
      .Times(AtLeast(1));
  EXPECT_CALL(*prefs, SetInt64(kCurrentBytesDownloadedFromHttp, 0))
      .Times(AtLeast(1));
  EXPECT_CALL(*prefs, SetInt64(kCurrentBytesDownloadedFromHttpPeer, 0))
      .Times(AtLeast(1));
  EXPECT_CALL(*prefs, SetInt64(kPrefsNumReboots, 0)).Times(AtLeast(1));
  PayloadState payload_state;
  EXPECT_TRUE(payload_state.Initialize());
  payload_state.SetResponse(response);
  string stored_response_sign = payload_state.GetResponseSignature();
  string expected_response_sign =
      "Max Failure Count Per Url = 0\n"
      "Disable Payload Backoff = 0\n";
  EXPECT_EQ(expected_response_sign, stored_response_sign);
  EXPECT_EQ("", payload_state.GetCurrentUrl());
  EXPECT_EQ(0U, payload_state.GetUrlFailureCount());
  EXPECT_EQ(0U, payload_state.GetUrlSwitchCount());
  EXPECT_EQ(1, payload_state.GetNumResponsesSeen());
}

TEST_F(PayloadStateTest, SetResponseWorksWithSingleUrl) {
  OmahaResponse response;
  response.packages.push_back({.payload_urls = {"https://single.url.test"},
                               .size = 123456789,
                               .metadata_size = 58123,
                               .metadata_signature = "msign",
                               .hash = "hash"});
  FakeSystemState::Get()->set_prefs(nullptr);
  auto* prefs = FakeSystemState::Get()->mock_prefs();
  EXPECT_CALL(*prefs, SetInt64(_, _)).Times(AnyNumber());
  EXPECT_CALL(*prefs, SetInt64(kPrefsPayloadAttemptNumber, 0))
      .Times(AtLeast(1));
  EXPECT_CALL(*prefs, SetInt64(kPrefsFullPayloadAttemptNumber, 0))
      .Times(AtLeast(1));
  EXPECT_CALL(*prefs, SetInt64(kPrefsBackoffExpiryTime, 0)).Times(AtLeast(1));
  EXPECT_CALL(*prefs, SetInt64(kPrefsCurrentUrlIndex, 0)).Times(AtLeast(1));
  EXPECT_CALL(*prefs, SetInt64(kPrefsCurrentUrlFailureCount, 0))
      .Times(AtLeast(1));
  EXPECT_CALL(*prefs, SetInt64(kPrefsUpdateTimestampStart, _))
      .Times(AtLeast(1));
  EXPECT_CALL(*prefs, SetInt64(kPrefsUpdateDurationUptime, _))
      .Times(AtLeast(1));
  EXPECT_CALL(*prefs, SetInt64(kCurrentBytesDownloadedFromHttps, 0))
      .Times(AtLeast(1));
  EXPECT_CALL(*prefs, SetInt64(kCurrentBytesDownloadedFromHttp, 0))
      .Times(AtLeast(1));
  EXPECT_CALL(*prefs, SetInt64(kCurrentBytesDownloadedFromHttpPeer, 0))
      .Times(AtLeast(1));
  EXPECT_CALL(*prefs, SetInt64(kPrefsNumReboots, 0)).Times(AtLeast(1));
  PayloadState payload_state;
  EXPECT_TRUE(payload_state.Initialize());
  payload_state.SetResponse(response);
  string stored_response_sign = payload_state.GetResponseSignature();
  string expected_response_sign =
      "Payload 0:\n"
      "  Size = 123456789\n"
      "  Sha256 Hash = hash\n"
      "  Metadata Size = 58123\n"
      "  Metadata Signature = msign\n"
      "  Is Delta = 0\n"
      "  NumURLs = 1\n"
      "  Candidate Url0 = https://single.url.test\n"
      "Max Failure Count Per Url = 0\n"
      "Disable Payload Backoff = 0\n";
  EXPECT_EQ(expected_response_sign, stored_response_sign);
  EXPECT_EQ("https://single.url.test", payload_state.GetCurrentUrl());
  EXPECT_EQ(0U, payload_state.GetUrlFailureCount());
  EXPECT_EQ(0U, payload_state.GetUrlSwitchCount());
  EXPECT_EQ(1, payload_state.GetNumResponsesSeen());
}

TEST_F(PayloadStateTest, SetResponseWorksWithMultipleUrls) {
  OmahaResponse response;
  response.packages.push_back({.payload_urls = {"http://multiple.url.test",
                                                "https://multiple.url.test"},
                               .size = 523456789,
                               .metadata_size = 558123,
                               .metadata_signature = "metasign",
                               .hash = "rhash"});
  FakeSystemState::Get()->set_prefs(nullptr);
  auto* prefs = FakeSystemState::Get()->mock_prefs();
  EXPECT_CALL(*prefs, SetInt64(_, _)).Times(AnyNumber());
  EXPECT_CALL(*prefs, SetInt64(kPrefsPayloadAttemptNumber, 0))
      .Times(AtLeast(1));
  EXPECT_CALL(*prefs, SetInt64(kPrefsFullPayloadAttemptNumber, 0))
      .Times(AtLeast(1));
  EXPECT_CALL(*prefs, SetInt64(kPrefsBackoffExpiryTime, 0)).Times(AtLeast(1));
  EXPECT_CALL(*prefs, SetInt64(kPrefsCurrentUrlIndex, 0)).Times(AtLeast(1));
  EXPECT_CALL(*prefs, SetInt64(kPrefsCurrentUrlFailureCount, 0))
      .Times(AtLeast(1));
  EXPECT_CALL(*prefs, SetInt64(kCurrentBytesDownloadedFromHttps, 0))
      .Times(AtLeast(1));
  EXPECT_CALL(*prefs, SetInt64(kCurrentBytesDownloadedFromHttp, 0))
      .Times(AtLeast(1));
  EXPECT_CALL(*prefs, SetInt64(kCurrentBytesDownloadedFromHttpPeer, 0))
      .Times(AtLeast(1));
  EXPECT_CALL(*prefs, SetInt64(kPrefsNumReboots, 0)).Times(AtLeast(1));

  PayloadState payload_state;
  EXPECT_TRUE(payload_state.Initialize());
  payload_state.SetResponse(response);
  string stored_response_sign = payload_state.GetResponseSignature();
  string expected_response_sign =
      "Payload 0:\n"
      "  Size = 523456789\n"
      "  Sha256 Hash = rhash\n"
      "  Metadata Size = 558123\n"
      "  Metadata Signature = metasign\n"
      "  Is Delta = 0\n"
      "  NumURLs = 2\n"
      "  Candidate Url0 = http://multiple.url.test\n"
      "  Candidate Url1 = https://multiple.url.test\n"
      "Max Failure Count Per Url = 0\n"
      "Disable Payload Backoff = 0\n";
  EXPECT_EQ(expected_response_sign, stored_response_sign);
  EXPECT_EQ("http://multiple.url.test", payload_state.GetCurrentUrl());
  EXPECT_EQ(0U, payload_state.GetUrlFailureCount());
  EXPECT_EQ(0U, payload_state.GetUrlSwitchCount());
  EXPECT_EQ(1, payload_state.GetNumResponsesSeen());
}

TEST_F(PayloadStateTest, CanAdvanceUrlIndexCorrectly) {
  OmahaResponse response;
  FakeSystemState::Get()->set_prefs(nullptr);
  auto* prefs = FakeSystemState::Get()->mock_prefs();
  PayloadState payload_state;

  EXPECT_CALL(*prefs, SetInt64(_, _)).Times(AnyNumber());
  // Payload attempt should start with 0 and then advance to 1.
  EXPECT_CALL(*prefs, SetInt64(kPrefsPayloadAttemptNumber, 0))
      .Times(AtLeast(1));
  EXPECT_CALL(*prefs, SetInt64(kPrefsPayloadAttemptNumber, 1))
      .Times(AtLeast(1));
  EXPECT_CALL(*prefs, SetInt64(kPrefsFullPayloadAttemptNumber, 0))
      .Times(AtLeast(1));
  EXPECT_CALL(*prefs, SetInt64(kPrefsFullPayloadAttemptNumber, 1))
      .Times(AtLeast(1));
  EXPECT_CALL(*prefs, SetInt64(kPrefsBackoffExpiryTime, _)).Times(AtLeast(2));

  // Reboots will be set
  EXPECT_CALL(*prefs, SetInt64(kPrefsNumReboots, _)).Times(AtLeast(1));

  // Url index should go from 0 to 1 twice.
  EXPECT_CALL(*prefs, SetInt64(kPrefsCurrentUrlIndex, 0)).Times(AtLeast(1));
  EXPECT_CALL(*prefs, SetInt64(kPrefsCurrentUrlIndex, 1)).Times(AtLeast(1));

  // Failure count should be called each times url index is set, so that's
  // 4 times for this test.
  EXPECT_CALL(*prefs, SetInt64(kPrefsCurrentUrlFailureCount, 0))
      .Times(AtLeast(4));

  EXPECT_TRUE(payload_state.Initialize());

  // This does a SetResponse which causes all the states to be set to 0 for
  // the first time.
  SetupPayloadStateWith2Urls("Hash1235", true, false, &payload_state,
                             &response);
  EXPECT_EQ("http://test", payload_state.GetCurrentUrl());

  // Verify that on the first error, the URL index advances to 1.
  ErrorCode error = ErrorCode::kDownloadMetadataSignatureMismatch;
  payload_state.UpdateFailed(error);
  EXPECT_EQ("https://test", payload_state.GetCurrentUrl());

  // Verify that on the next error, the URL index wraps around to 0.
  payload_state.UpdateFailed(error);
  EXPECT_EQ("http://test", payload_state.GetCurrentUrl());

  // Verify that on the next error, it again advances to 1.
  payload_state.UpdateFailed(error);
  EXPECT_EQ("https://test", payload_state.GetCurrentUrl());

  // Verify that we switched URLs three times
  EXPECT_EQ(3U, payload_state.GetUrlSwitchCount());
}

TEST_F(PayloadStateTest, NewResponseResetsPayloadState) {
  OmahaResponse response;
  PayloadState payload_state;

  EXPECT_TRUE(payload_state.Initialize());

  // Set the first response.
  SetupPayloadStateWith2Urls("Hash5823", true, false, &payload_state,
                             &response);
  EXPECT_EQ(1, payload_state.GetNumResponsesSeen());

  // Advance the URL index to 1 by faking an error.
  ErrorCode error = ErrorCode::kDownloadMetadataSignatureMismatch;
  payload_state.UpdateFailed(error);
  EXPECT_EQ("https://test", payload_state.GetCurrentUrl());
  EXPECT_EQ(1U, payload_state.GetUrlSwitchCount());

  // Now, slightly change the response and set it again.
  SetupPayloadStateWith2Urls("Hash8225", true, false, &payload_state,
                             &response);
  EXPECT_EQ(2, payload_state.GetNumResponsesSeen());

  // Fake an error again.
  payload_state.UpdateFailed(error);
  EXPECT_EQ("https://test", payload_state.GetCurrentUrl());
  EXPECT_EQ(1U, payload_state.GetUrlSwitchCount());

  // Return a third different response.
  SetupPayloadStateWith2Urls("Hash9999", true, false, &payload_state,
                             &response);
  EXPECT_EQ(3, payload_state.GetNumResponsesSeen());

  // Make sure the url index was reset to 0 because of the new response.
  EXPECT_EQ("http://test", payload_state.GetCurrentUrl());
  EXPECT_EQ(0U, payload_state.GetUrlFailureCount());
  EXPECT_EQ(0U, payload_state.GetUrlSwitchCount());
  EXPECT_EQ(0U,
            payload_state.GetCurrentBytesDownloaded(kDownloadSourceHttpServer));
  EXPECT_EQ(0U,
            payload_state.GetTotalBytesDownloaded(kDownloadSourceHttpServer));
  EXPECT_EQ(
      0U, payload_state.GetCurrentBytesDownloaded(kDownloadSourceHttpsServer));
  EXPECT_EQ(0U,
            payload_state.GetTotalBytesDownloaded(kDownloadSourceHttpsServer));
}

TEST_F(PayloadStateTest, AllCountersGetUpdatedProperlyOnErrorCodesAndEvents) {
  OmahaResponse response;
  PayloadState payload_state;
  int progress_bytes = 100;
  FakeSystemState::Get()->set_prefs(nullptr);
  auto* prefs = FakeSystemState::Get()->mock_prefs();

  EXPECT_CALL(*prefs, SetInt64(_, _)).Times(AnyNumber());
  EXPECT_CALL(*prefs, SetInt64(kPrefsPayloadAttemptNumber, 0))
      .Times(AtLeast(2));
  EXPECT_CALL(*prefs, SetInt64(kPrefsPayloadAttemptNumber, 1))
      .Times(AtLeast(1));
  EXPECT_CALL(*prefs, SetInt64(kPrefsPayloadAttemptNumber, 2))
      .Times(AtLeast(1));

  EXPECT_CALL(*prefs, SetInt64(kPrefsFullPayloadAttemptNumber, 0))
      .Times(AtLeast(2));
  EXPECT_CALL(*prefs, SetInt64(kPrefsFullPayloadAttemptNumber, 1))
      .Times(AtLeast(1));
  EXPECT_CALL(*prefs, SetInt64(kPrefsFullPayloadAttemptNumber, 2))
      .Times(AtLeast(1));

  EXPECT_CALL(*prefs, SetInt64(kPrefsBackoffExpiryTime, _)).Times(AtLeast(4));

  EXPECT_CALL(*prefs, SetInt64(kPrefsCurrentUrlIndex, 0)).Times(AtLeast(4));
  EXPECT_CALL(*prefs, SetInt64(kPrefsCurrentUrlIndex, 1)).Times(AtLeast(2));

  EXPECT_CALL(*prefs, SetInt64(kPrefsCurrentUrlFailureCount, 0))
      .Times(AtLeast(7));
  EXPECT_CALL(*prefs, SetInt64(kPrefsCurrentUrlFailureCount, 1))
      .Times(AtLeast(2));
  EXPECT_CALL(*prefs, SetInt64(kPrefsCurrentUrlFailureCount, 2))
      .Times(AtLeast(1));

  EXPECT_CALL(*prefs, SetInt64(kPrefsUpdateTimestampStart, _))
      .Times(AtLeast(1));
  EXPECT_CALL(*prefs, SetInt64(kPrefsUpdateDurationUptime, _))
      .Times(AtLeast(1));

  EXPECT_CALL(*prefs, SetInt64(kCurrentBytesDownloadedFromHttps, 0))
      .Times(AtLeast(1));
  EXPECT_CALL(*prefs, SetInt64(kCurrentBytesDownloadedFromHttp, 0))
      .Times(AtLeast(1));
  EXPECT_CALL(*prefs, SetInt64(kCurrentBytesDownloadedFromHttpPeer, 0))
      .Times(AtLeast(1));
  EXPECT_CALL(*prefs, SetInt64(kCurrentBytesDownloadedFromHttp, progress_bytes))
      .Times(AtLeast(1));
  EXPECT_CALL(*prefs, SetInt64(kTotalBytesDownloadedFromHttp, progress_bytes))
      .Times(AtLeast(1));
  EXPECT_CALL(*prefs, SetInt64(kPrefsNumReboots, 0)).Times(AtLeast(1));

  EXPECT_TRUE(payload_state.Initialize());

  SetupPayloadStateWith2Urls("Hash5873", true, false, &payload_state,
                             &response);
  EXPECT_EQ(1, payload_state.GetNumResponsesSeen());

  // This should advance the URL index.
  payload_state.UpdateFailed(ErrorCode::kDownloadMetadataSignatureMismatch);
  EXPECT_EQ(0, payload_state.GetPayloadAttemptNumber());
  EXPECT_EQ(0, payload_state.GetFullPayloadAttemptNumber());
  EXPECT_EQ("https://test", payload_state.GetCurrentUrl());
  EXPECT_EQ(0U, payload_state.GetUrlFailureCount());
  EXPECT_EQ(1U, payload_state.GetUrlSwitchCount());

  // This should advance the failure count only.
  payload_state.UpdateFailed(ErrorCode::kDownloadTransferError);
  EXPECT_EQ(0, payload_state.GetPayloadAttemptNumber());
  EXPECT_EQ(0, payload_state.GetFullPayloadAttemptNumber());
  EXPECT_EQ("https://test", payload_state.GetCurrentUrl());
  EXPECT_EQ(1U, payload_state.GetUrlFailureCount());
  EXPECT_EQ(1U, payload_state.GetUrlSwitchCount());

  // This should advance the failure count only.
  payload_state.UpdateFailed(ErrorCode::kDownloadTransferError);
  EXPECT_EQ(0, payload_state.GetPayloadAttemptNumber());
  EXPECT_EQ(0, payload_state.GetFullPayloadAttemptNumber());
  EXPECT_EQ("https://test", payload_state.GetCurrentUrl());
  EXPECT_EQ(2U, payload_state.GetUrlFailureCount());
  EXPECT_EQ(1U, payload_state.GetUrlSwitchCount());

  // This should advance the URL index as we've reached the
  // max failure count and reset the failure count for the new URL index.
  // This should also wrap around the URL index and thus cause the payload
  // attempt number to be incremented.
  payload_state.UpdateFailed(ErrorCode::kDownloadTransferError);
  EXPECT_EQ(1, payload_state.GetPayloadAttemptNumber());
  EXPECT_EQ(1, payload_state.GetFullPayloadAttemptNumber());
  EXPECT_EQ("http://test", payload_state.GetCurrentUrl());
  EXPECT_EQ(0U, payload_state.GetUrlFailureCount());
  EXPECT_EQ(2U, payload_state.GetUrlSwitchCount());
  EXPECT_TRUE(payload_state.ShouldBackoffDownload());

  // This should advance the URL index.
  payload_state.UpdateFailed(ErrorCode::kPayloadHashMismatchError);
  EXPECT_EQ(1, payload_state.GetPayloadAttemptNumber());
  EXPECT_EQ(1, payload_state.GetFullPayloadAttemptNumber());
  EXPECT_EQ("https://test", payload_state.GetCurrentUrl());
  EXPECT_EQ(0U, payload_state.GetUrlFailureCount());
  EXPECT_EQ(3U, payload_state.GetUrlSwitchCount());
  EXPECT_TRUE(payload_state.ShouldBackoffDownload());

  // This should advance the URL index and payload attempt number due to
  // wrap-around of URL index.
  payload_state.UpdateFailed(ErrorCode::kDownloadMetadataSignatureMissingError);
  EXPECT_EQ(2, payload_state.GetPayloadAttemptNumber());
  EXPECT_EQ(2, payload_state.GetFullPayloadAttemptNumber());
  EXPECT_EQ("http://test", payload_state.GetCurrentUrl());
  EXPECT_EQ(0U, payload_state.GetUrlFailureCount());
  EXPECT_EQ(4U, payload_state.GetUrlSwitchCount());
  EXPECT_TRUE(payload_state.ShouldBackoffDownload());

  // This HTTP error code should only increase the failure count.
  payload_state.UpdateFailed(static_cast<ErrorCode>(
      static_cast<int>(ErrorCode::kOmahaRequestHTTPResponseBase) + 404));
  EXPECT_EQ(2, payload_state.GetPayloadAttemptNumber());
  EXPECT_EQ(2, payload_state.GetFullPayloadAttemptNumber());
  EXPECT_EQ("http://test", payload_state.GetCurrentUrl());
  EXPECT_EQ(1U, payload_state.GetUrlFailureCount());
  EXPECT_EQ(4U, payload_state.GetUrlSwitchCount());
  EXPECT_TRUE(payload_state.ShouldBackoffDownload());

  // And that failure count should be reset when we download some bytes
  // afterwards.
  payload_state.DownloadProgress(progress_bytes);
  EXPECT_EQ(2, payload_state.GetPayloadAttemptNumber());
  EXPECT_EQ(2, payload_state.GetFullPayloadAttemptNumber());
  EXPECT_EQ("http://test", payload_state.GetCurrentUrl());
  EXPECT_EQ(0U, payload_state.GetUrlFailureCount());
  EXPECT_EQ(4U, payload_state.GetUrlSwitchCount());
  EXPECT_TRUE(payload_state.ShouldBackoffDownload());

  // Now, slightly change the response and set it again.
  SetupPayloadStateWith2Urls("Hash8532", true, false, &payload_state,
                             &response);
  EXPECT_EQ(2, payload_state.GetNumResponsesSeen());

  // Make sure the url index was reset to 0 because of the new response.
  EXPECT_EQ(0, payload_state.GetPayloadAttemptNumber());
  EXPECT_EQ(0, payload_state.GetFullPayloadAttemptNumber());
  EXPECT_EQ("http://test", payload_state.GetCurrentUrl());
  EXPECT_EQ(0U, payload_state.GetUrlFailureCount());
  EXPECT_EQ(0U, payload_state.GetUrlSwitchCount());
  EXPECT_FALSE(payload_state.ShouldBackoffDownload());
}

TEST_F(PayloadStateTest,
       PayloadAttemptNumberIncreasesOnSuccessfulFullDownload) {
  OmahaResponse response;
  PayloadState payload_state;
  FakeSystemState::Get()->set_prefs(nullptr);
  auto* prefs = FakeSystemState::Get()->mock_prefs();

  EXPECT_CALL(*prefs, SetInt64(_, _)).Times(AnyNumber());
  EXPECT_CALL(*prefs, SetInt64(kPrefsPayloadAttemptNumber, 0))
      .Times(AtLeast(1));
  EXPECT_CALL(*prefs, SetInt64(kPrefsPayloadAttemptNumber, 1))
      .Times(AtLeast(1));

  EXPECT_CALL(*prefs, SetInt64(kPrefsFullPayloadAttemptNumber, 0))
      .Times(AtLeast(1));
  EXPECT_CALL(*prefs, SetInt64(kPrefsFullPayloadAttemptNumber, 1))
      .Times(AtLeast(1));

  EXPECT_CALL(*prefs, SetInt64(kPrefsBackoffExpiryTime, _)).Times(AtLeast(2));

  EXPECT_CALL(*prefs, SetInt64(kPrefsCurrentUrlIndex, 0)).Times(AtLeast(1));
  EXPECT_CALL(*prefs, SetInt64(kPrefsCurrentUrlFailureCount, 0))
      .Times(AtLeast(1));

  EXPECT_TRUE(payload_state.Initialize());

  SetupPayloadStateWith2Urls("Hash8593", true, false, &payload_state,
                             &response);

  // This should just advance the payload attempt number;
  EXPECT_EQ(0, payload_state.GetPayloadAttemptNumber());
  EXPECT_EQ(0, payload_state.GetFullPayloadAttemptNumber());
  payload_state.DownloadComplete();
  EXPECT_EQ(1, payload_state.GetPayloadAttemptNumber());
  EXPECT_EQ(1, payload_state.GetFullPayloadAttemptNumber());
  EXPECT_EQ("http://test", payload_state.GetCurrentUrl());
  EXPECT_EQ(0U, payload_state.GetUrlFailureCount());
  EXPECT_EQ(0U, payload_state.GetUrlSwitchCount());
}

TEST_F(PayloadStateTest,
       PayloadAttemptNumberIncreasesOnSuccessfulDeltaDownload) {
  OmahaResponse response;
  PayloadState payload_state;
  FakeSystemState::Get()->set_prefs(nullptr);
  auto* prefs = FakeSystemState::Get()->mock_prefs();

  EXPECT_CALL(*prefs, SetInt64(_, _)).Times(AnyNumber());
  EXPECT_CALL(*prefs, SetInt64(kPrefsPayloadAttemptNumber, 0))
      .Times(AtLeast(1));
  EXPECT_CALL(*prefs, SetInt64(kPrefsPayloadAttemptNumber, 1))
      .Times(AtLeast(1));

  // kPrefsFullPayloadAttemptNumber is not incremented for delta payloads.
  EXPECT_CALL(*prefs, SetInt64(kPrefsFullPayloadAttemptNumber, 0))
      .Times(AtLeast(1));

  EXPECT_CALL(*prefs, SetInt64(kPrefsBackoffExpiryTime, _)).Times(1);

  EXPECT_CALL(*prefs, SetInt64(kPrefsCurrentUrlIndex, 0)).Times(AtLeast(1));
  EXPECT_CALL(*prefs, SetInt64(kPrefsCurrentUrlFailureCount, 0))
      .Times(AtLeast(1));

  EXPECT_TRUE(payload_state.Initialize());

  SetupPayloadStateWith2Urls("Hash8593", true, true, &payload_state, &response);

  // This should just advance the payload attempt number;
  EXPECT_EQ(0, payload_state.GetPayloadAttemptNumber());
  EXPECT_EQ(0, payload_state.GetFullPayloadAttemptNumber());
  payload_state.DownloadComplete();
  EXPECT_EQ(1, payload_state.GetPayloadAttemptNumber());
  EXPECT_EQ(0, payload_state.GetFullPayloadAttemptNumber());
  EXPECT_EQ("http://test", payload_state.GetCurrentUrl());
  EXPECT_EQ(0U, payload_state.GetUrlFailureCount());
  EXPECT_EQ(0U, payload_state.GetUrlSwitchCount());
}

TEST_F(PayloadStateTest, SetResponseResetsInvalidUrlIndex) {
  OmahaResponse response;
  PayloadState payload_state;

  EXPECT_TRUE(payload_state.Initialize());
  SetupPayloadStateWith2Urls("Hash4427", true, false, &payload_state,
                             &response);

  // Generate enough events to advance URL index, failure count and
  // payload attempt number all to 1.
  payload_state.DownloadComplete();
  payload_state.UpdateFailed(ErrorCode::kDownloadMetadataSignatureMismatch);
  payload_state.UpdateFailed(ErrorCode::kDownloadTransferError);
  EXPECT_EQ(1, payload_state.GetPayloadAttemptNumber());
  EXPECT_EQ(1, payload_state.GetFullPayloadAttemptNumber());
  EXPECT_EQ("https://test", payload_state.GetCurrentUrl());
  EXPECT_EQ(1U, payload_state.GetUrlFailureCount());
  EXPECT_EQ(1U, payload_state.GetUrlSwitchCount());

  // Now, simulate a corrupted url index on persisted store which gets
  // loaded when update_engine restarts.
  FakeSystemState::Get()->set_prefs(nullptr);
  auto* prefs = FakeSystemState::Get()->mock_prefs();
  EXPECT_CALL(*prefs, Exists(_)).WillRepeatedly(Return(true));
  EXPECT_CALL(*prefs, GetInt64(_, _)).Times(AtLeast(1));
  EXPECT_CALL(*prefs, GetInt64(kPrefsPayloadAttemptNumber, _))
      .Times(AtLeast(1));
  EXPECT_CALL(*prefs, GetInt64(kPrefsFullPayloadAttemptNumber, _))
      .Times(AtLeast(1));
  EXPECT_CALL(*prefs, GetInt64(kPrefsCurrentUrlIndex, _))
      .WillRepeatedly(DoAll(SetArgPointee<1>(2), Return(true)));
  EXPECT_CALL(*prefs, GetInt64(kPrefsCurrentUrlFailureCount, _))
      .Times(AtLeast(1));
  EXPECT_CALL(*prefs, GetInt64(kPrefsUrlSwitchCount, _)).Times(AtLeast(1));

  // Note: This will be a different payload object, but the response should
  // have the same hash as before so as to not trivially reset because the
  // response was different. We want to specifically test that even if the
  // response is same, we should reset the state if we find it corrupted.
  EXPECT_TRUE(payload_state.Initialize());
  SetupPayloadStateWith2Urls("Hash4427", true, false, &payload_state,
                             &response);

  // Make sure all counters get reset to 0 because of the corrupted URL index
  // we supplied above.
  EXPECT_EQ(0, payload_state.GetPayloadAttemptNumber());
  EXPECT_EQ(0, payload_state.GetFullPayloadAttemptNumber());
  EXPECT_EQ("http://test", payload_state.GetCurrentUrl());
  EXPECT_EQ(0U, payload_state.GetUrlFailureCount());
  EXPECT_EQ(0U, payload_state.GetUrlSwitchCount());
}

TEST_F(PayloadStateTest, NoBackoffInteractiveChecks) {
  OmahaResponse response;
  PayloadState payload_state;
  OmahaRequestParams params;
  params.Init("", "", {.interactive = true});
  FakeSystemState::Get()->set_request_params(&params);

  EXPECT_TRUE(payload_state.Initialize());
  SetupPayloadStateWith2Urls("Hash6437", true, false, &payload_state,
                             &response);

  // Simulate two failures (enough to cause payload backoff) and check
  // again that we're ready to re-download without any backoff as this is
  // an interactive check.
  payload_state.UpdateFailed(ErrorCode::kDownloadMetadataSignatureMismatch);
  payload_state.UpdateFailed(ErrorCode::kDownloadMetadataSignatureMismatch);
  EXPECT_EQ("http://test", payload_state.GetCurrentUrl());
  EXPECT_EQ(1, payload_state.GetPayloadAttemptNumber());
  EXPECT_EQ(1, payload_state.GetFullPayloadAttemptNumber());
  EXPECT_FALSE(payload_state.ShouldBackoffDownload());
}

TEST_F(PayloadStateTest, NoBackoffForP2PUpdates) {
  OmahaResponse response;
  PayloadState payload_state;
  OmahaRequestParams params;
  params.Init("", "", {});
  FakeSystemState::Get()->set_request_params(&params);

  EXPECT_TRUE(payload_state.Initialize());
  SetupPayloadStateWith2Urls("Hash6437", true, false, &payload_state,
                             &response);

  // Simulate two failures (enough to cause payload backoff) and check
  // again that we're ready to re-download without any backoff as this is
  // an interactive check.
  payload_state.UpdateFailed(ErrorCode::kDownloadMetadataSignatureMismatch);
  payload_state.UpdateFailed(ErrorCode::kDownloadMetadataSignatureMismatch);
  EXPECT_EQ("http://test", payload_state.GetCurrentUrl());
  EXPECT_EQ(1, payload_state.GetPayloadAttemptNumber());
  EXPECT_EQ(1, payload_state.GetFullPayloadAttemptNumber());
  // Set p2p url.
  payload_state.SetUsingP2PForDownloading(true);
  payload_state.SetP2PUrl("http://mypeer:52909/path/to/file");
  // Should not backoff for p2p updates.
  EXPECT_FALSE(payload_state.ShouldBackoffDownload());

  payload_state.SetP2PUrl("");
  // No actual p2p update if no url is provided.
  EXPECT_TRUE(payload_state.ShouldBackoffDownload());
}

TEST_F(PayloadStateTest, NoBackoffForDeltaPayloads) {
  OmahaResponse response;
  PayloadState payload_state;

  EXPECT_TRUE(payload_state.Initialize());
  SetupPayloadStateWith2Urls("Hash6437", true, true, &payload_state, &response);

  // Simulate a successful download and see that we're ready to download
  // again without any backoff as this is a delta payload.
  payload_state.DownloadComplete();
  EXPECT_EQ(1, payload_state.GetPayloadAttemptNumber());
  EXPECT_EQ(0, payload_state.GetFullPayloadAttemptNumber());
  EXPECT_FALSE(payload_state.ShouldBackoffDownload());

  // Simulate two failures (enough to cause payload backoff) and check
  // again that we're ready to re-download without any backoff as this is
  // a delta payload.
  payload_state.UpdateFailed(ErrorCode::kDownloadMetadataSignatureMismatch);
  payload_state.UpdateFailed(ErrorCode::kDownloadMetadataSignatureMismatch);
  EXPECT_EQ("http://test", payload_state.GetCurrentUrl());
  EXPECT_EQ(2, payload_state.GetPayloadAttemptNumber());
  EXPECT_EQ(0, payload_state.GetFullPayloadAttemptNumber());
  EXPECT_FALSE(payload_state.ShouldBackoffDownload());
}

static void CheckPayloadBackoffState(PayloadState* payload_state,
                                     int expected_attempt_number,
                                     TimeDelta expected_days) {
  payload_state->DownloadComplete();
  EXPECT_EQ(expected_attempt_number,
            payload_state->GetFullPayloadAttemptNumber());
  EXPECT_TRUE(payload_state->ShouldBackoffDownload());
  Time backoff_expiry_time = payload_state->GetBackoffExpiryTime();
  // Add 1 hour extra to the 6 hour fuzz check to tolerate edge cases.
  TimeDelta max_fuzz_delta = base::Hours(7);
  Time expected_min_time = Time::Now() + expected_days - max_fuzz_delta;
  Time expected_max_time = Time::Now() + expected_days + max_fuzz_delta;
  EXPECT_LT(expected_min_time.ToInternalValue(),
            backoff_expiry_time.ToInternalValue());
  EXPECT_GT(expected_max_time.ToInternalValue(),
            backoff_expiry_time.ToInternalValue());
}

TEST_F(PayloadStateTest, BackoffPeriodsAreInCorrectRange) {
  OmahaResponse response;
  PayloadState payload_state;

  EXPECT_TRUE(payload_state.Initialize());
  SetupPayloadStateWith2Urls("Hash8939", true, false, &payload_state,
                             &response);

  CheckPayloadBackoffState(&payload_state, 1, base::Days(1));
  CheckPayloadBackoffState(&payload_state, 2, base::Days(2));
  CheckPayloadBackoffState(&payload_state, 3, base::Days(4));
  CheckPayloadBackoffState(&payload_state, 4, base::Days(8));
  CheckPayloadBackoffState(&payload_state, 5, base::Days(16));
  CheckPayloadBackoffState(&payload_state, 6, base::Days(16));
  CheckPayloadBackoffState(&payload_state, 7, base::Days(16));
  CheckPayloadBackoffState(&payload_state, 8, base::Days(16));
  CheckPayloadBackoffState(&payload_state, 9, base::Days(16));
  CheckPayloadBackoffState(&payload_state, 10, base::Days(16));
}

TEST_F(PayloadStateTest, BackoffLogicCanBeDisabled) {
  OmahaResponse response;
  response.disable_payload_backoff = true;
  PayloadState payload_state;

  EXPECT_TRUE(payload_state.Initialize());
  SetupPayloadStateWith2Urls("Hash8939", true, false, &payload_state,
                             &response);

  // Simulate a successful download and see that we are ready to download
  // again without any backoff.
  payload_state.DownloadComplete();
  EXPECT_EQ(1, payload_state.GetPayloadAttemptNumber());
  EXPECT_EQ(1, payload_state.GetFullPayloadAttemptNumber());
  EXPECT_FALSE(payload_state.ShouldBackoffDownload());

  // Test again, this time by simulating two errors that would cause
  // the payload attempt number to increment due to wrap around. And
  // check that we are still ready to re-download without any backoff.
  payload_state.UpdateFailed(ErrorCode::kDownloadMetadataSignatureMismatch);
  payload_state.UpdateFailed(ErrorCode::kDownloadMetadataSignatureMismatch);
  EXPECT_EQ(2, payload_state.GetPayloadAttemptNumber());
  EXPECT_EQ(2, payload_state.GetFullPayloadAttemptNumber());
  EXPECT_FALSE(payload_state.ShouldBackoffDownload());
}

TEST_F(PayloadStateTest, BytesDownloadedMetricsGetAddedToCorrectSources) {
  OmahaResponse response;
  response.disable_payload_backoff = true;
  PayloadState payload_state;
  uint64_t https_total = 0;
  uint64_t http_total = 0;

  EXPECT_TRUE(payload_state.Initialize());
  SetupPayloadStateWith2Urls("Hash3286", true, false, &payload_state,
                             &response);
  EXPECT_EQ(1, payload_state.GetNumResponsesSeen());

  // Simulate a previous attempt with in order to set an initial non-zero value
  // for the total bytes downloaded for HTTP.
  uint64_t prev_chunk = 323456789;
  http_total += prev_chunk;
  payload_state.DownloadProgress(prev_chunk);

  // Ensure that the initial values for HTTP reflect this attempt.
  EXPECT_EQ(prev_chunk,
            payload_state.GetCurrentBytesDownloaded(kDownloadSourceHttpServer));
  EXPECT_EQ(http_total,
            payload_state.GetTotalBytesDownloaded(kDownloadSourceHttpServer));

  // Change the response hash so as to simulate a new response which will
  // reset the current bytes downloaded, but not the total bytes downloaded.
  SetupPayloadStateWith2Urls("Hash9904", true, false, &payload_state,
                             &response);
  EXPECT_EQ(2, payload_state.GetNumResponsesSeen());

  // First, simulate successful download of a few bytes over HTTP.
  uint64_t first_chunk = 5000000;
  http_total += first_chunk;
  payload_state.DownloadProgress(first_chunk);
  // Test that first all progress is made on HTTP and none on HTTPS.
  EXPECT_EQ(first_chunk,
            payload_state.GetCurrentBytesDownloaded(kDownloadSourceHttpServer));
  EXPECT_EQ(http_total,
            payload_state.GetTotalBytesDownloaded(kDownloadSourceHttpServer));
  EXPECT_EQ(
      0U, payload_state.GetCurrentBytesDownloaded(kDownloadSourceHttpsServer));
  EXPECT_EQ(https_total,
            payload_state.GetTotalBytesDownloaded(kDownloadSourceHttpsServer));

  // Simulate an error that'll cause the url index to point to https.
  ErrorCode error = ErrorCode::kDownloadMetadataSignatureMismatch;
  payload_state.UpdateFailed(error);

  // Test that no new progress is made on HTTP and new progress is on HTTPS.
  uint64_t second_chunk = 23456789;
  https_total += second_chunk;
  payload_state.DownloadProgress(second_chunk);
  EXPECT_EQ(first_chunk,
            payload_state.GetCurrentBytesDownloaded(kDownloadSourceHttpServer));
  EXPECT_EQ(http_total,
            payload_state.GetTotalBytesDownloaded(kDownloadSourceHttpServer));
  EXPECT_EQ(second_chunk, payload_state.GetCurrentBytesDownloaded(
                              kDownloadSourceHttpsServer));
  EXPECT_EQ(https_total,
            payload_state.GetTotalBytesDownloaded(kDownloadSourceHttpsServer));

  // Simulate error to go back to http.
  payload_state.UpdateFailed(error);
  uint64_t third_chunk = 32345678;
  uint64_t http_chunk = first_chunk + third_chunk;
  http_total += third_chunk;
  payload_state.DownloadProgress(third_chunk);

  // Test that third chunk is again back on HTTP. HTTPS remains on second chunk.
  EXPECT_EQ(http_chunk,
            payload_state.GetCurrentBytesDownloaded(kDownloadSourceHttpServer));
  EXPECT_EQ(http_total,
            payload_state.GetTotalBytesDownloaded(kDownloadSourceHttpServer));
  EXPECT_EQ(second_chunk, payload_state.GetCurrentBytesDownloaded(
                              kDownloadSourceHttpsServer));
  EXPECT_EQ(https_total,
            payload_state.GetTotalBytesDownloaded(kDownloadSourceHttpsServer));

  // Simulate error (will cause URL switch), set p2p is to be used and
  // then do 42MB worth of progress
  payload_state.UpdateFailed(error);
  payload_state.SetUsingP2PForDownloading(true);
  uint64_t p2p_total = 42 * 1000 * 1000;
  payload_state.DownloadProgress(p2p_total);

  EXPECT_EQ(p2p_total,
            payload_state.GetTotalBytesDownloaded(kDownloadSourceHttpPeer));

  EXPECT_CALL(*FakeSystemState::Get()->mock_metrics_reporter(),
              ReportSuccessfulUpdateMetrics(1, _, kPayloadTypeFull, _, _, 314,
                                            _, _, _, 3));

  payload_state.UpdateSucceeded();

  // Make sure the metrics are reset after a successful update.
  EXPECT_EQ(0U,
            payload_state.GetCurrentBytesDownloaded(kDownloadSourceHttpServer));
  EXPECT_EQ(0U,
            payload_state.GetTotalBytesDownloaded(kDownloadSourceHttpServer));
  EXPECT_EQ(
      0U, payload_state.GetCurrentBytesDownloaded(kDownloadSourceHttpsServer));
  EXPECT_EQ(0U,
            payload_state.GetTotalBytesDownloaded(kDownloadSourceHttpsServer));
  EXPECT_EQ(0, payload_state.GetNumResponsesSeen());
}

TEST_F(PayloadStateTest, DownloadSourcesUsedIsCorrect) {
  OmahaResponse response;
  PayloadState payload_state;

  EXPECT_TRUE(payload_state.Initialize());
  SetupPayloadStateWith2Urls("Hash3286", true, false, &payload_state,
                             &response);

  // Simulate progress in order to mark HTTP as one of the sources used.
  uint64_t num_bytes = 42 * 1000 * 1000;
  payload_state.DownloadProgress(num_bytes);

  // Check that this was done via HTTP.
  EXPECT_EQ(num_bytes,
            payload_state.GetCurrentBytesDownloaded(kDownloadSourceHttpServer));
  EXPECT_EQ(num_bytes,
            payload_state.GetTotalBytesDownloaded(kDownloadSourceHttpServer));

  // Check that only HTTP is reported as a download source.
  int64_t total_bytes[kNumDownloadSources] = {};
  total_bytes[kDownloadSourceHttpServer] = num_bytes;

  EXPECT_CALL(*FakeSystemState::Get()->mock_metrics_reporter(),
              ReportSuccessfulUpdateMetrics(
                  _, _, _, _, test_utils::DownloadSourceMatcher(total_bytes), _,
                  _, _, _, _))
      .Times(1);

  payload_state.UpdateSucceeded();
}

TEST_F(PayloadStateTest, RestartingUpdateResetsMetrics) {
  OmahaResponse response;
  PayloadState payload_state;

  EXPECT_TRUE(payload_state.Initialize());

  // Set the first response.
  SetupPayloadStateWith2Urls("Hash5823", true, false, &payload_state,
                             &response);

  uint64_t num_bytes = 10000;
  payload_state.DownloadProgress(num_bytes);
  EXPECT_EQ(num_bytes,
            payload_state.GetCurrentBytesDownloaded(kDownloadSourceHttpServer));
  EXPECT_EQ(num_bytes,
            payload_state.GetTotalBytesDownloaded(kDownloadSourceHttpServer));
  EXPECT_EQ(
      0U, payload_state.GetCurrentBytesDownloaded(kDownloadSourceHttpsServer));
  EXPECT_EQ(0U,
            payload_state.GetTotalBytesDownloaded(kDownloadSourceHttpsServer));

  payload_state.UpdateRestarted();
  // Make sure the current bytes downloaded is reset, but not the total bytes.
  EXPECT_EQ(0U,
            payload_state.GetCurrentBytesDownloaded(kDownloadSourceHttpServer));
  EXPECT_EQ(num_bytes,
            payload_state.GetTotalBytesDownloaded(kDownloadSourceHttpServer));
}

TEST_F(PayloadStateTest, NumRebootsIncrementsCorrectly) {
  FakeSystemState::Get()->set_prefs(nullptr);
  auto* prefs = FakeSystemState::Get()->mock_prefs();
  EXPECT_CALL(*prefs, SetInt64(_, _)).Times(AtLeast(0));
  EXPECT_CALL(*prefs, SetInt64(kPrefsNumReboots, 1)).Times(AtLeast(1));

  PayloadState payload_state;
  EXPECT_TRUE(payload_state.Initialize());

  payload_state.UpdateRestarted();
  EXPECT_EQ(0U, payload_state.GetNumReboots());

  FakeSystemState::Get()->set_system_rebooted(true);
  payload_state.UpdateResumed();
  // Num reboots should be incremented because system rebooted detected.
  EXPECT_EQ(1U, payload_state.GetNumReboots());

  FakeSystemState::Get()->set_system_rebooted(false);
  payload_state.UpdateResumed();
  // Num reboots should now be 1 as reboot was not detected.
  EXPECT_EQ(1U, payload_state.GetNumReboots());

  // Restart the update again to verify we set the num of reboots back to 0.
  payload_state.UpdateRestarted();
  EXPECT_EQ(0U, payload_state.GetNumReboots());
}

TEST_F(PayloadStateTest, RollbackHappened) {
  FakeSystemState::Get()->set_powerwash_safe_prefs(nullptr);
  auto* mock_powerwash_safe_prefs =
      FakeSystemState::Get()->mock_powerwash_safe_prefs();
  PayloadState payload_state;
  EXPECT_TRUE(payload_state.Initialize());

  // Verify pre-conditions are good.
  EXPECT_FALSE(payload_state.GetRollbackHappened());

  // Set to true.
  EXPECT_CALL(*mock_powerwash_safe_prefs,
              SetBoolean(kPrefsRollbackHappened, true));
  payload_state.SetRollbackHappened(true);
  EXPECT_TRUE(payload_state.GetRollbackHappened());

  // Set to false.
  EXPECT_CALL(*mock_powerwash_safe_prefs, Delete(kPrefsRollbackHappened));
  payload_state.SetRollbackHappened(false);
  EXPECT_FALSE(payload_state.GetRollbackHappened());

  // Let's verify we can reload it correctly.
  EXPECT_CALL(*mock_powerwash_safe_prefs, GetBoolean(kPrefsRollbackHappened, _))
      .WillOnce(DoAll(SetArgPointee<1>(true), Return(true)));
  EXPECT_CALL(*mock_powerwash_safe_prefs,
              SetBoolean(kPrefsRollbackHappened, true));
  payload_state.LoadRollbackHappened();
  EXPECT_TRUE(payload_state.GetRollbackHappened());
}

TEST_F(PayloadStateTest, RollbackVersion) {
  FakeSystemState::Get()->set_powerwash_safe_prefs(nullptr);
  auto* mock_powerwash_safe_prefs =
      FakeSystemState::Get()->mock_powerwash_safe_prefs();

  // Mock out the os version and make sure it's excluded correctly.
  string rollback_version = "2345.0.0";
  OmahaRequestParams params;
  params.Init(rollback_version, "", {});
  FakeSystemState::Get()->set_request_params(&params);

  PayloadState payload_state;
  EXPECT_TRUE(payload_state.Initialize());

  // Verify pre-conditions are good.
  EXPECT_TRUE(payload_state.GetRollbackVersion().empty());

  EXPECT_CALL(*mock_powerwash_safe_prefs,
              SetString(kPrefsRollbackVersion, rollback_version));
  payload_state.Rollback();

  EXPECT_EQ(rollback_version, payload_state.GetRollbackVersion());

  // Change it up a little and verify we load it correctly.
  rollback_version = "2345.0.1";
  // Let's verify we can reload it correctly.
  EXPECT_CALL(*mock_powerwash_safe_prefs, GetString(kPrefsRollbackVersion, _))
      .WillOnce(DoAll(SetArgPointee<1>(rollback_version), Return(true)));
  EXPECT_CALL(*mock_powerwash_safe_prefs,
              SetString(kPrefsRollbackVersion, rollback_version));
  payload_state.LoadRollbackVersion();
  EXPECT_EQ(rollback_version, payload_state.GetRollbackVersion());

  // Check that we report only UpdateEngine.Rollback.* metrics in
  // UpdateSucceeded().
  EXPECT_CALL(*FakeSystemState::Get()->mock_metrics_reporter(),
              ReportRollbackMetrics(metrics::RollbackResult::kSuccess))
      .Times(1);

  payload_state.UpdateSucceeded();
}

TEST_F(PayloadStateTest, DurationsAreCorrect) {
  OmahaResponse response;
  response.packages.resize(1);

  // Set the clock to a well-known time - 1 second on the wall-clock
  // and 2 seconds on the monotonic clock
  auto* fake_clock = FakeSystemState::Get()->fake_clock();
  fake_clock->SetWallclockTime(Time::FromInternalValue(1000000));
  fake_clock->SetMonotonicTime(Time::FromInternalValue(2000000));

  PayloadState payload_state;
  EXPECT_TRUE(payload_state.Initialize());

  // Check that durations are correct for a successful update where
  // time has advanced 7 seconds on the wall clock and 4 seconds on
  // the monotonic clock.
  SetupPayloadStateWith2Urls("Hash8593", true, false, &payload_state,
                             &response);
  fake_clock->SetWallclockTime(Time::FromInternalValue(8000000));
  fake_clock->SetMonotonicTime(Time::FromInternalValue(6000000));
  payload_state.UpdateSucceeded();
  EXPECT_EQ(payload_state.GetUpdateDuration().InMicroseconds(), 7000000);
  EXPECT_EQ(payload_state.GetUpdateDurationUptime().InMicroseconds(), 4000000);

  // Check that durations are reset when a new response comes in.
  SetupPayloadStateWith2Urls("Hash8594", true, false, &payload_state,
                             &response);
  EXPECT_EQ(payload_state.GetUpdateDuration().InMicroseconds(), 0);
  EXPECT_EQ(payload_state.GetUpdateDurationUptime().InMicroseconds(), 0);

  // Advance time a bit (10 secs), simulate download progress and
  // check that durations are updated.
  fake_clock->SetWallclockTime(Time::FromInternalValue(18000000));
  fake_clock->SetMonotonicTime(Time::FromInternalValue(16000000));
  payload_state.DownloadProgress(10);
  EXPECT_EQ(payload_state.GetUpdateDuration().InMicroseconds(), 10000000);
  EXPECT_EQ(payload_state.GetUpdateDurationUptime().InMicroseconds(), 10000000);

  // Now simulate a reboot by resetting monotonic time (to 5000) and
  // creating a new PayloadState object and check that we load the
  // durations correctly (e.g. they are the same as before).
  fake_clock->SetMonotonicTime(Time::FromInternalValue(5000));
  PayloadState payload_state2;
  EXPECT_TRUE(payload_state2.Initialize());
  payload_state2.SetResponse(response);
  EXPECT_EQ(payload_state2.GetUpdateDuration().InMicroseconds(), 10000000);
  EXPECT_EQ(payload_state2.GetUpdateDurationUptime().InMicroseconds(),
            10000000);

  // Advance wall-clock by 7 seconds and monotonic clock by 6 seconds
  // and check that the durations are increased accordingly.
  fake_clock->SetWallclockTime(Time::FromInternalValue(25000000));
  fake_clock->SetMonotonicTime(Time::FromInternalValue(6005000));
  payload_state2.UpdateSucceeded();
  EXPECT_EQ(payload_state2.GetUpdateDuration().InMicroseconds(), 17000000);
  EXPECT_EQ(payload_state2.GetUpdateDurationUptime().InMicroseconds(),
            16000000);
}

TEST_F(PayloadStateTest, RestartAfterCrash) {
  PayloadState payload_state;
  testing::StrictMock<MockMetricsReporter> mock_metrics_reporter;
  FakeSystemState::Get()->set_metrics_reporter(&mock_metrics_reporter);
  FakeSystemState::Get()->set_prefs(nullptr);
  auto* prefs = FakeSystemState::Get()->mock_prefs();

  EXPECT_TRUE(payload_state.Initialize());

  // Only the |kPrefsAttemptInProgress| state variable should be read.
  EXPECT_CALL(*prefs, Exists(_)).Times(0);
  EXPECT_CALL(*prefs, SetString(_, _)).Times(0);
  EXPECT_CALL(*prefs, SetInt64(_, _)).Times(0);
  EXPECT_CALL(*prefs, SetBoolean(_, _)).Times(0);
  EXPECT_CALL(*prefs, GetString(_, _)).Times(0);
  EXPECT_CALL(*prefs, GetInt64(_, _)).Times(0);
  EXPECT_CALL(*prefs, GetBoolean(_, _)).Times(0);
  EXPECT_CALL(*prefs, GetBoolean(kPrefsAttemptInProgress, _));

  // Simulate an update_engine restart without a reboot.
  FakeSystemState::Get()->set_system_rebooted(false);

  payload_state.UpdateEngineStarted();
}

TEST_F(PayloadStateTest, AbnormalTerminationAttemptMetricsNoReporting) {
  PayloadState payload_state;

  // If there's no marker at startup, ensure we don't report a metric.
  EXPECT_TRUE(payload_state.Initialize());
  EXPECT_CALL(*FakeSystemState::Get()->mock_metrics_reporter(),
              ReportAbnormallyTerminatedUpdateAttemptMetrics())
      .Times(0);
  payload_state.UpdateEngineStarted();
}

TEST_F(PayloadStateTest, AbnormalTerminationAttemptMetricsReported) {
  // If we have a marker at startup, ensure it's reported and the
  // marker is then cleared.
  auto* fake_prefs = FakeSystemState::Get()->fake_prefs();
  fake_prefs->SetBoolean(kPrefsAttemptInProgress, true);

  PayloadState payload_state;
  EXPECT_TRUE(payload_state.Initialize());

  EXPECT_CALL(*FakeSystemState::Get()->mock_metrics_reporter(),
              ReportAbnormallyTerminatedUpdateAttemptMetrics())
      .Times(1);
  payload_state.UpdateEngineStarted();

  EXPECT_FALSE(fake_prefs->Exists(kPrefsAttemptInProgress));
}

TEST_F(PayloadStateTest, AbnormalTerminationAttemptMetricsClearedOnSucceess) {
  // Make sure the marker is written and cleared during an attempt and
  // also that we DO NOT emit the metric (since the attempt didn't end
  // abnormally).
  PayloadState payload_state;
  EXPECT_TRUE(payload_state.Initialize());
  OmahaResponse response;
  response.packages.resize(1);
  payload_state.SetResponse(response);

  EXPECT_CALL(*FakeSystemState::Get()->mock_metrics_reporter(),
              ReportAbnormallyTerminatedUpdateAttemptMetrics())
      .Times(0);

  auto* fake_prefs = FakeSystemState::Get()->fake_prefs();
  // Attempt not in progress, should be clear.
  EXPECT_FALSE(fake_prefs->Exists(kPrefsAttemptInProgress));

  payload_state.UpdateRestarted();

  // Attempt not in progress, should be set.
  EXPECT_TRUE(fake_prefs->Exists(kPrefsAttemptInProgress));

  payload_state.UpdateSucceeded();

  // Attempt not in progress, should be clear.
  EXPECT_FALSE(fake_prefs->Exists(kPrefsAttemptInProgress));
}

TEST_F(PayloadStateTest, CandidateUrlsMissingErrorReported) {
  PayloadState payload_state;
  ErrorCode error = ErrorCode::kNonCriticalUpdateInOOBE;
  EXPECT_CALL(*FakeSystemState::Get()->mock_metrics_reporter(),
              ReportInternalErrorCode(error));
  payload_state.UpdateFailed(error);
}

TEST_F(PayloadStateTest, CandidateUrlsMissingErrorNotReportedForSuccessCode) {
  PayloadState payload_state;
  ErrorCode error = ErrorCode::kSuccess;
  EXPECT_CALL(*FakeSystemState::Get()->mock_metrics_reporter(),
              ReportInternalErrorCode(error))
      .Times(0);
  payload_state.UpdateFailed(error);
}

TEST_F(PayloadStateTest, ErrorsGenerateAlerts) {
  base::test::MockLog mock_log;
  mock_log.StartCapturingLogs();
  EXPECT_CALL(mock_log, Log(_, _, _, _, _)).Times(AnyNumber());

  PayloadState payload_state;
  EXPECT_TRUE(payload_state.Initialize());
  OmahaResponse response;
  SetupPayloadStateWith2Urls("Hash1235", true, false, &payload_state,
                             &response);

  EXPECT_CALL(mock_log, Log(::logging::LOGGING_ERROR, _, _, _,
                            HasSubstr("UpdateEngineAlert")));
  payload_state.UpdateFailed(ErrorCode::kPayloadHashMismatchError);
}

TEST_F(PayloadStateTest, ErrorsGenerateAlertsWithoutAnyCandidateUrls) {
  base::test::MockLog mock_log;
  mock_log.StartCapturingLogs();
  EXPECT_CALL(mock_log, Log(_, _, _, _, _)).Times(AnyNumber());

  PayloadState payload_state;

  EXPECT_CALL(mock_log, Log(::logging::LOGGING_ERROR, _, _, _,
                            HasSubstr("UpdateEngineAlert")));
  payload_state.UpdateFailed(ErrorCode::kPayloadHashMismatchError);
}

TEST_F(PayloadStateTest, CandidateUrlsComputedCorrectly) {
  OmahaResponse response;
  PayloadState payload_state;

  policy::MockDevicePolicy disable_http_policy;
  FakeSystemState::Get()->set_device_policy(&disable_http_policy);
  EXPECT_TRUE(payload_state.Initialize());

  // Test with no device policy. Should default to allowing http.
  EXPECT_CALL(disable_http_policy, GetHttpDownloadsEnabled(_))
      .WillRepeatedly(Return(false));

  // Set the first response.
  SetupPayloadStateWith2Urls("Hash8433", true, false, &payload_state,
                             &response);

  // Check that we use the HTTP URL since there is no value set for allowing
  // http.
  EXPECT_EQ("http://test", payload_state.GetCurrentUrl());

  // Test with device policy not allowing http updates.
  EXPECT_CALL(disable_http_policy, GetHttpDownloadsEnabled(_))
      .WillRepeatedly(DoAll(SetArgPointee<0>(false), Return(true)));

  // Reset state and set again.
  SetupPayloadStateWith2Urls("Hash8433", false, false, &payload_state,
                             &response);

  // Check that we skip the HTTP URL and use only the HTTPS url.
  EXPECT_EQ("https://test", payload_state.GetCurrentUrl());

  // Advance the URL index to 1 by faking an error.
  ErrorCode error = ErrorCode::kDownloadMetadataSignatureMismatch;
  payload_state.UpdateFailed(error);

  // Check that we still skip the HTTP URL and use only the HTTPS url.
  EXPECT_EQ("https://test", payload_state.GetCurrentUrl());
  EXPECT_EQ(0U, payload_state.GetUrlSwitchCount());

  // Now, slightly change the response and set it again.
  SetupPayloadStateWith2Urls("Hash2399", false, false, &payload_state,
                             &response);

  // Check that we still skip the HTTP URL and use only the HTTPS url.
  EXPECT_EQ("https://test", payload_state.GetCurrentUrl());

  // Now, pretend that the HTTP policy is turned on. We want to make sure
  // the new policy is honored.
  policy::MockDevicePolicy enable_http_policy;
  FakeSystemState::Get()->set_device_policy(&enable_http_policy);
  EXPECT_CALL(enable_http_policy, GetHttpDownloadsEnabled(_))
      .WillRepeatedly(DoAll(SetArgPointee<0>(true), Return(true)));

  // Now, set the same response using the same hash
  // so that we can test that the state is reset not because of the
  // hash but because of the policy change which results in candidate url
  // list change.
  SetupPayloadStateWith2Urls("Hash2399", true, false, &payload_state,
                             &response);

  // Check that we use the HTTP URL now and the failure count is reset.
  EXPECT_EQ("http://test", payload_state.GetCurrentUrl());
  EXPECT_EQ(0U, payload_state.GetUrlFailureCount());

  // Fake a failure and see if we're moving over to the HTTPS url and update
  // the URL switch count properly.
  payload_state.UpdateFailed(error);
  EXPECT_EQ("https://test", payload_state.GetCurrentUrl());
  EXPECT_EQ(1U, payload_state.GetUrlSwitchCount());
  EXPECT_EQ(0U, payload_state.GetUrlFailureCount());
}

TEST_F(PayloadStateTest, PayloadTypeMetricWhenTypeIsDelta) {
  OmahaResponse response;
  PayloadState payload_state;

  EXPECT_TRUE(payload_state.Initialize());
  SetupPayloadStateWith2Urls("Hash6437", true, true, &payload_state, &response);

  // Simulate a successful download and update.
  payload_state.DownloadComplete();
  EXPECT_CALL(*FakeSystemState::Get()->mock_metrics_reporter(),
              ReportSuccessfulUpdateMetrics(_, _, kPayloadTypeDelta, _, _, _, _,
                                            _, _, _));
  payload_state.UpdateSucceeded();

  // Mock the request to a request where the delta was disabled but Omaha sends
  // a delta anyway and test again.
  OmahaRequestParams params;
  params.set_delta_okay(false);
  FakeSystemState::Get()->set_request_params(&params);

  EXPECT_TRUE(payload_state.Initialize());
  SetupPayloadStateWith2Urls("Hash6437", true, true, &payload_state, &response);

  payload_state.DownloadComplete();

  EXPECT_CALL(*FakeSystemState::Get()->mock_metrics_reporter(),
              ReportSuccessfulUpdateMetrics(_, _, kPayloadTypeDelta, _, _, _, _,
                                            _, _, _));
  payload_state.UpdateSucceeded();
}

TEST_F(PayloadStateTest, PayloadTypeMetricWhenTypeIsForcedFull) {
  OmahaResponse response;
  PayloadState payload_state;

  // Mock the request to a request where the delta was disabled.
  OmahaRequestParams params;
  params.set_delta_okay(false);
  FakeSystemState::Get()->set_request_params(&params);

  EXPECT_TRUE(payload_state.Initialize());
  SetupPayloadStateWith2Urls("Hash6437", true, false, &payload_state,
                             &response);

  // Simulate a successful download and update.
  payload_state.DownloadComplete();

  EXPECT_CALL(*FakeSystemState::Get()->mock_metrics_reporter(),
              ReportSuccessfulUpdateMetrics(_, _, kPayloadTypeForcedFull, _, _,
                                            _, _, _, _, _));
  payload_state.UpdateSucceeded();
}

TEST_F(PayloadStateTest, PayloadTypeMetricWhenTypeIsFull) {
  OmahaResponse response;
  PayloadState payload_state;

  EXPECT_TRUE(payload_state.Initialize());
  SetupPayloadStateWith2Urls("Hash6437", true, false, &payload_state,
                             &response);

  // Mock the request to a request where the delta is enabled, although the
  // result is full.
  OmahaRequestParams params;
  params.set_delta_okay(true);
  FakeSystemState::Get()->set_request_params(&params);

  // Simulate a successful download and update.
  payload_state.DownloadComplete();

  EXPECT_CALL(*FakeSystemState::Get()->mock_metrics_reporter(),
              ReportSuccessfulUpdateMetrics(_, _, kPayloadTypeFull, _, _, _, _,
                                            _, _, _));
  payload_state.UpdateSucceeded();
}

TEST_F(PayloadStateTest, RebootAfterUpdateFailedMetric) {
  OmahaResponse response;
  PayloadState payload_state;
  EXPECT_TRUE(payload_state.Initialize());
  SetupPayloadStateWith2Urls("Hash3141", true, false, &payload_state,
                             &response);

  // Simulate a successful download and update.
  payload_state.DownloadComplete();
  payload_state.UpdateSucceeded();
  payload_state.ExpectRebootInNewVersion("Version:12345678");

  // Reboot into the same environment to get an UMA metric with a value of 1.
  EXPECT_CALL(*FakeSystemState::Get()->mock_metrics_reporter(),
              ReportFailedUpdateCount(1));
  payload_state.ReportFailedBootIfNeeded();
  Mock::VerifyAndClearExpectations(
      FakeSystemState::Get()->mock_metrics_reporter());

  // Simulate a second update and reboot into the same environment, this should
  // send a value of 2.
  payload_state.ExpectRebootInNewVersion("Version:12345678");

  EXPECT_CALL(*FakeSystemState::Get()->mock_metrics_reporter(),
              ReportFailedUpdateCount(2));
  payload_state.ReportFailedBootIfNeeded();
  Mock::VerifyAndClearExpectations(
      FakeSystemState::Get()->mock_metrics_reporter());

  // Simulate a third failed reboot to new version, but this time for a
  // different payload. This should send a value of 1 this time.
  payload_state.ExpectRebootInNewVersion("Version:3141592");
  EXPECT_CALL(*FakeSystemState::Get()->mock_metrics_reporter(),
              ReportFailedUpdateCount(1));
  payload_state.ReportFailedBootIfNeeded();
  Mock::VerifyAndClearExpectations(
      FakeSystemState::Get()->mock_metrics_reporter());
}

TEST_F(PayloadStateTest, RebootAfterUpdateSucceed) {
  OmahaResponse response;
  PayloadState payload_state;
  FakeBootControl* fake_boot_control =
      FakeSystemState::Get()->fake_boot_control();
  fake_boot_control->SetCurrentSlot(0);

  EXPECT_TRUE(payload_state.Initialize());
  SetupPayloadStateWith2Urls("Hash3141", true, false, &payload_state,
                             &response);

  // Simulate a successful download and update.
  payload_state.DownloadComplete();
  payload_state.UpdateSucceeded();
  payload_state.ExpectRebootInNewVersion("Version:12345678");

  // Change the BootDevice to a different one, no metric should be sent.
  fake_boot_control->SetCurrentSlot(1);

  EXPECT_CALL(*FakeSystemState::Get()->mock_metrics_reporter(),
              ReportFailedUpdateCount(_))
      .Times(0);
  payload_state.ReportFailedBootIfNeeded();

  // A second reboot in either partition should not send a metric.
  payload_state.ReportFailedBootIfNeeded();
  fake_boot_control->SetCurrentSlot(0);
  payload_state.ReportFailedBootIfNeeded();
}

TEST_F(PayloadStateTest, RebootAfterCanceledUpdate) {
  OmahaResponse response;
  PayloadState payload_state;
  EXPECT_TRUE(payload_state.Initialize());
  SetupPayloadStateWith2Urls("Hash3141", true, false, &payload_state,
                             &response);

  // Simulate a successful download and update.
  payload_state.DownloadComplete();
  payload_state.UpdateSucceeded();
  payload_state.ExpectRebootInNewVersion("Version:12345678");

  EXPECT_CALL(*FakeSystemState::Get()->mock_metrics_reporter(),
              ReportFailedUpdateCount(_))
      .Times(0);

  // Cancel the applied update.
  payload_state.ResetUpdateStatus();

  // Simulate a reboot.
  payload_state.ReportFailedBootIfNeeded();
}

TEST_F(PayloadStateTest, UpdateSuccessWithWipedPrefs) {
  PayloadState payload_state;
  EXPECT_TRUE(payload_state.Initialize());

  EXPECT_CALL(*FakeSystemState::Get()->mock_metrics_reporter(),
              ReportFailedUpdateCount(_))
      .Times(0);

  // Simulate a reboot in this environment.
  payload_state.ReportFailedBootIfNeeded();
}

TEST_F(PayloadStateTest, DisallowP2PAfterTooManyAttempts) {
  OmahaResponse response;
  PayloadState payload_state;
  EXPECT_TRUE(payload_state.Initialize());
  SetupPayloadStateWith2Urls("Hash8593", true, false, &payload_state,
                             &response);

  // Should allow exactly kMaxP2PAttempts...
  for (int n = 0; n < kMaxP2PAttempts; n++) {
    payload_state.P2PNewAttempt();
    EXPECT_TRUE(payload_state.P2PAttemptAllowed());
  }
  // ... but not more than that.
  payload_state.P2PNewAttempt();
  EXPECT_FALSE(payload_state.P2PAttemptAllowed());
}

TEST_F(PayloadStateTest, DisallowP2PAfterDeadline) {
  OmahaResponse response;
  PayloadState payload_state;
  EXPECT_TRUE(payload_state.Initialize());
  SetupPayloadStateWith2Urls("Hash8593", true, false, &payload_state,
                             &response);

  // Set the clock to 1 second.
  Time epoch = Time::FromInternalValue(1000000);
  auto* fake_clock = FakeSystemState::Get()->fake_clock();
  fake_clock->SetWallclockTime(epoch);

  // Do an attempt - this will set the timestamp.
  payload_state.P2PNewAttempt();

  // Check that the timestamp equals what we just set.
  EXPECT_EQ(epoch, payload_state.GetP2PFirstAttemptTimestamp());

  // Time hasn't advanced - this should work.
  EXPECT_TRUE(payload_state.P2PAttemptAllowed());

  // Set clock to half the deadline - this should work.
  fake_clock->SetWallclockTime(epoch + kMaxP2PAttemptTime / 2);
  EXPECT_TRUE(payload_state.P2PAttemptAllowed());

  // Check that the first attempt timestamp hasn't changed just
  // because the wall-clock time changed.
  EXPECT_EQ(epoch, payload_state.GetP2PFirstAttemptTimestamp());

  // Set clock to _just_ before the deadline - this should work.
  fake_clock->SetWallclockTime(epoch + kMaxP2PAttemptTime - base::Seconds(1));
  EXPECT_TRUE(payload_state.P2PAttemptAllowed());

  // Set clock to _just_ after the deadline - this should not work.
  fake_clock->SetWallclockTime(epoch + kMaxP2PAttemptTime + base::Seconds(1));
  EXPECT_FALSE(payload_state.P2PAttemptAllowed());
}

TEST_F(PayloadStateTest, P2PStateVarsInitialValue) {
  OmahaResponse response;
  PayloadState payload_state;
  EXPECT_TRUE(payload_state.Initialize());
  SetupPayloadStateWith2Urls("Hash8593", true, false, &payload_state,
                             &response);

  Time null_time = Time();
  EXPECT_EQ(null_time, payload_state.GetP2PFirstAttemptTimestamp());
  EXPECT_EQ(0, payload_state.GetP2PNumAttempts());
}

TEST_F(PayloadStateTest, P2PStateVarsArePersisted) {
  OmahaResponse response;
  PayloadState payload_state;
  EXPECT_TRUE(payload_state.Initialize());
  SetupPayloadStateWith2Urls("Hash8593", true, false, &payload_state,
                             &response);

  // Set the clock to something known.
  Time time = Time::FromInternalValue(12345);
  FakeSystemState::Get()->fake_clock()->SetWallclockTime(time);

  // New p2p attempt - as a side-effect this will update the p2p state vars.
  payload_state.P2PNewAttempt();
  EXPECT_EQ(1, payload_state.GetP2PNumAttempts());
  EXPECT_EQ(time, payload_state.GetP2PFirstAttemptTimestamp());

  // Now create a new PayloadState and check that it loads the state
  // vars correctly.
  PayloadState payload_state2;
  EXPECT_TRUE(payload_state2.Initialize());
  EXPECT_EQ(1, payload_state2.GetP2PNumAttempts());
  EXPECT_EQ(time, payload_state2.GetP2PFirstAttemptTimestamp());
}

TEST_F(PayloadStateTest, P2PStateVarsAreClearedOnNewResponse) {
  OmahaResponse response;
  PayloadState payload_state;
  EXPECT_TRUE(payload_state.Initialize());
  SetupPayloadStateWith2Urls("Hash8593", true, false, &payload_state,
                             &response);

  // Set the clock to something known.
  Time time = Time::FromInternalValue(12345);
  FakeSystemState::Get()->fake_clock()->SetWallclockTime(time);

  // New p2p attempt - as a side-effect this will update the p2p state vars.
  payload_state.P2PNewAttempt();
  EXPECT_EQ(1, payload_state.GetP2PNumAttempts());
  EXPECT_EQ(time, payload_state.GetP2PFirstAttemptTimestamp());

  // Set a new response...
  SetupPayloadStateWith2Urls("Hash9904", true, false, &payload_state,
                             &response);

  // ... and check that it clears the P2P state vars.
  Time null_time = Time();
  EXPECT_EQ(0, payload_state.GetP2PNumAttempts());
  EXPECT_EQ(null_time, payload_state.GetP2PFirstAttemptTimestamp());
}

TEST_F(PayloadStateTest, NextPayloadResetsUrlIndex) {
  PayloadState payload_state;
  StrictMock<MockExcluder> mock_excluder;
  EXPECT_CALL(*FakeSystemState::Get()->mock_update_attempter(), GetExcluder())
      .WillOnce(Return(&mock_excluder));
  EXPECT_TRUE(payload_state.Initialize());

  OmahaResponse response;
  response.packages.push_back(
      {.payload_urls = {"http://test1a", "http://test2a"},
       .size = 123456789,
       .metadata_size = 58123,
       .metadata_signature = "msign",
       .hash = "hash"});
  response.packages.push_back({.payload_urls = {"http://test1b"},
                               .size = 123456789,
                               .metadata_size = 58123,
                               .metadata_signature = "msign",
                               .hash = "hash"});
  payload_state.SetResponse(response);

  EXPECT_EQ(payload_state.GetCurrentUrl(), "http://test1a");
  payload_state.IncrementUrlIndex();
  EXPECT_EQ(payload_state.GetCurrentUrl(), "http://test2a");

  EXPECT_TRUE(payload_state.NextPayload());
  EXPECT_EQ(payload_state.GetCurrentUrl(), "http://test1b");
}

TEST_F(PayloadStateTest, ExcludeNoopForNonExcludables) {
  PayloadState payload_state;
  StrictMock<MockExcluder> mock_excluder;
  EXPECT_CALL(*FakeSystemState::Get()->mock_update_attempter(), GetExcluder())
      .WillOnce(Return(&mock_excluder));
  EXPECT_TRUE(payload_state.Initialize());

  OmahaResponse response;
  response.packages.push_back(
      {.payload_urls = {"http://test1a", "http://test2a"},
       .size = 123456789,
       .metadata_size = 58123,
       .metadata_signature = "msign",
       .hash = "hash",
       .can_exclude = false});
  payload_state.SetResponse(response);

  EXPECT_CALL(mock_excluder, Exclude(_)).Times(0);
  payload_state.ExcludeCurrentPayload();
}

TEST_F(PayloadStateTest, ExcludeOnlyCanExcludables) {
  PayloadState payload_state;
  StrictMock<MockExcluder> mock_excluder;
  EXPECT_CALL(*FakeSystemState::Get()->mock_update_attempter(), GetExcluder())
      .WillOnce(Return(&mock_excluder));
  EXPECT_TRUE(payload_state.Initialize());

  OmahaResponse response;
  response.packages.push_back(
      {.payload_urls = {"http://test1a", "http://test2a"},
       .size = 123456789,
       .metadata_size = 58123,
       .metadata_signature = "msign",
       .hash = "hash",
       .can_exclude = true});
  payload_state.SetResponse(response);

  EXPECT_CALL(mock_excluder, Exclude(utils::GetExclusionName("http://test1a")))
      .WillOnce(Return(true));
  payload_state.ExcludeCurrentPayload();
}

TEST_F(PayloadStateTest, IncrementFailureExclusionTest) {
  PayloadState payload_state;
  StrictMock<MockExcluder> mock_excluder;
  EXPECT_CALL(*FakeSystemState::Get()->mock_update_attempter(), GetExcluder())
      .WillOnce(Return(&mock_excluder));
  EXPECT_TRUE(payload_state.Initialize());

  OmahaResponse response;
  // Critical package.
  response.packages.push_back(
      {.payload_urls = {"http://crit-test1a", "http://crit-test2a"},
       .size = 123456789,
       .metadata_size = 58123,
       .metadata_signature = "msign",
       .hash = "hash",
       .can_exclude = false});
  // Non-critical package.
  response.packages.push_back(
      {.payload_urls = {"http://test1a", "http://test2a"},
       .size = 123456789,
       .metadata_size = 58123,
       .metadata_signature = "msign",
       .hash = "hash",
       .can_exclude = true});
  response.max_failure_count_per_url = 2;
  payload_state.SetResponse(response);

  // Critical package won't be excluded.
  // Increment twice as failure count allowed per URL is set to 2.
  payload_state.IncrementFailureCount();
  payload_state.IncrementFailureCount();

  EXPECT_TRUE(payload_state.NextPayload());

  // First increment failure should not exclude.
  payload_state.IncrementFailureCount();

  // Second increment failure should exclude.
  EXPECT_CALL(mock_excluder, Exclude(utils::GetExclusionName("http://test1a")))
      .WillOnce(Return(true));
  payload_state.IncrementFailureCount();
}

TEST_F(PayloadStateTest, HaltExclusionPostPayloadExhaustion) {
  PayloadState payload_state;
  StrictMock<MockExcluder> mock_excluder;
  EXPECT_CALL(*FakeSystemState::Get()->mock_update_attempter(), GetExcluder())
      .WillOnce(Return(&mock_excluder));
  EXPECT_TRUE(payload_state.Initialize());

  OmahaResponse response;
  // Non-critical package.
  response.packages.push_back(
      {.payload_urls = {"http://test1a", "http://test2a"},
       .size = 123456789,
       .metadata_size = 58123,
       .metadata_signature = "msign",
       .hash = "hash",
       .can_exclude = true});
  payload_state.SetResponse(response);

  // Exclusion should be called when excluded.
  EXPECT_CALL(mock_excluder, Exclude(utils::GetExclusionName("http://test1a")))
      .WillOnce(Return(true));
  payload_state.ExcludeCurrentPayload();

  // No more paylods to go through.
  EXPECT_FALSE(payload_state.NextPayload());

  // Exclusion should not be called as all |Payload|s are exhausted.
  payload_state.ExcludeCurrentPayload();
}

TEST_F(PayloadStateTest, NonInfinitePayloadIndexIncrement) {
  PayloadState payload_state;
  EXPECT_TRUE(payload_state.Initialize());

  payload_state.SetResponse({});

  EXPECT_FALSE(payload_state.NextPayload());
  int payload_index = payload_state.payload_index_;

  EXPECT_FALSE(payload_state.NextPayload());
  EXPECT_EQ(payload_index, payload_state.payload_index_);
}

}  // namespace chromeos_update_engine
