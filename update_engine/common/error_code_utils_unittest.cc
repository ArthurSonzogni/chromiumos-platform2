//
// Copyright (C) 2023 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#include "update_engine/common/error_code_utils.h"

#include <unordered_map>

#include <base/test/mock_log.h>
#include <gtest/gtest.h>

namespace chromeos_update_engine::utils {

using ::testing::_;
using ::testing::HasSubstr;
using ::testing::StrictMock;

TEST(ErrorCodeUtilsTest, AlertLogTagCreationTest) {
  auto category = "test_category";
  EXPECT_EQ(base::StringPrintf("[UpdateEngineAlert<%s>] ", category),
            GenerateAlertTag(category));

  auto sub_category = "test_sub_category";
  EXPECT_EQ(
      base::StringPrintf("[UpdateEngineAlert<%s:%s>] ", category, sub_category),
      GenerateAlertTag(category, sub_category));

  auto detail1 = "detail_1";
  auto detail2 = "detail_2";
  EXPECT_EQ(base::StringPrintf("[UpdateEngineAlert<%s:%s:%s:%s>] ",
                               category,
                               sub_category,
                               detail1,
                               detail2),
            GenerateAlertTag(category, sub_category, detail1, detail2));
}

TEST(ErrorCodeUtilsTest, LogAlertTagTest) {
  base::test::MockLog mock_log;
  mock_log.StartCapturingLogs();
  EXPECT_CALL(
      mock_log,
      Log(::logging::LOGGING_ERROR,
          _,
          _,
          _,
          HasSubstr(GenerateAlertTag(kCategoryPayload, kErrorMismatch))));

  ErrorCode code = ErrorCode::kPayloadHashMismatchError;

  LogAlertTag(code);
}

TEST(ErrorCodeUtilsTest, NoLogAlertForUnsupportedError) {
  // Use strict mock since we expect no log alerts here.
  StrictMock<base::test::MockLog> mock_log;
  mock_log.StartCapturingLogs();

  ErrorCode code = ErrorCode::kDownloadTransferError;

  LogAlertTag(code);
}

class ErrorCodeAlertDetailsTest : public testing::TestWithParam<ErrorCode> {
  void SetUp() { mock_log_.StartCapturingLogs(); }

 protected:
  std::unordered_map<ErrorCode, std::string> expected_alerts_ = {
      {ErrorCode::kPayloadHashMismatchError,
       GenerateAlertTag(kCategoryPayload, kErrorMismatch)},
      {ErrorCode::kSignedDeltaPayloadExpectedError,
       GenerateAlertTag(kCategoryPayload, kErrorVerification)},
      {ErrorCode::kUnsupportedMajorPayloadVersion,
       GenerateAlertTag(kCategoryPayload, kErrorVersion)},
      {ErrorCode::kPayloadTimestampError,
       GenerateAlertTag(kCategoryPayload, kErrorTimestamp)},
      {ErrorCode::kDownloadInvalidMetadataMagicString,
       GenerateAlertTag(kCategoryDownload, kErrorSignature)},
      {ErrorCode::kDownloadOperationHashVerificationError,
       GenerateAlertTag(kCategoryDownload, kErrorVerification)},
      {ErrorCode::kDownloadSignatureMissingInManifest,
       GenerateAlertTag(kCategoryDownload, kErrorManifest)},
      {ErrorCode::kDownloadOperationExecutionError,
       GenerateAlertTag(kCategoryDownload)},
      {ErrorCode::kVerityCalculationError, GenerateAlertTag(kCategoryVerity)},
  };

  base::test::MockLog mock_log_;
};

TEST_P(ErrorCodeAlertDetailsTest, VerifyAlertDetails) {
  auto err = GetParam();
  EXPECT_CALL(
      mock_log_,
      Log(::logging::LOGGING_ERROR, _, _, _, HasSubstr(expected_alerts_[err])));
  LogAlertTag(err);
}

INSTANTIATE_TEST_SUITE_P(
    UniqueAlertErrorCodes,
    ErrorCodeAlertDetailsTest,
    ::testing::Values(ErrorCode::kPayloadHashMismatchError,
                      ErrorCode::kSignedDeltaPayloadExpectedError,
                      ErrorCode::kUnsupportedMajorPayloadVersion,
                      ErrorCode::kPayloadTimestampError,
                      ErrorCode::kDownloadInvalidMetadataMagicString,
                      ErrorCode::kDownloadOperationHashVerificationError,
                      ErrorCode::kDownloadSignatureMissingInManifest,
                      ErrorCode::kDownloadOperationExecutionError));

}  // namespace chromeos_update_engine::utils
