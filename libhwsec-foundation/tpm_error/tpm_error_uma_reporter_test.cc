// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libhwsec-foundation/tpm_error/tpm_error_uma_reporter.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <metrics/metrics_library_mock.h>

#include "libhwsec-foundation/tpm_error/tpm_error_constants.h"
#include "libhwsec-foundation/tpm_error/tpm_error_metrics_constants.h"

namespace hwsec_foundation {

namespace {
constexpr uint32_t kFakeCommand = 123;
}

using ::testing::StrictMock;

class TpmErrorUmaReporterTest : public ::testing::Test {
 public:
  TpmErrorUmaReporterTest() = default;
  ~TpmErrorUmaReporterTest() override = default;

 protected:
  StrictMock<MetricsLibraryMock> mock_metrics_library_;
  TpmErrorUmaReporter reporter_{&mock_metrics_library_};
};

TEST_F(TpmErrorUmaReporterTest, ReportTpm1AuthFail) {
  TpmErrorData data;
  data.command = kFakeCommand;
  data.response = kTpm1AuthFailResponse;
  EXPECT_CALL(mock_metrics_library_,
              SendSparseToUMA(kTpm1AuthFailName, data.command));
  reporter_.Report(data);
}

TEST_F(TpmErrorUmaReporterTest, ReportTpm1Auth2Fail) {
  TpmErrorData data;
  data.command = kFakeCommand;
  data.response = kTpm1Auth2FailResponse;
  EXPECT_CALL(mock_metrics_library_,
              SendSparseToUMA(kTpm1Auth2FailName, data.command));
  reporter_.Report(data);
}

TEST_F(TpmErrorUmaReporterTest, ReportNoFailure) {
  TpmErrorData data;
  data.command = kFakeCommand;
  data.response = 777;
  ASSERT_NE(data.response, kTpm1AuthFailResponse);
  ASSERT_NE(data.response, kTpm1Auth2FailResponse);
  // Expect no metrics is reported; strict mock will verify.
  reporter_.Report(data);
}

}  // namespace hwsec_foundation
