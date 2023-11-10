// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <utility>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <base/files/file_path.h>
#include <base/files/scoped_temp_dir.h>
#include <metrics/metrics_library_mock.h>

#include "minios/metrics_reporter.h"

using ::testing::_;
using ::testing::StrictMock;

namespace minios {

class MetricsReporterTest : public ::testing::Test {
 protected:
  std::unique_ptr<MetricsLibraryMock> metrics_library_mock_ =
      std::make_unique<StrictMock<MetricsLibraryMock>>();
  MetricsLibraryMock* metrics_library_mock_ptr_ = metrics_library_mock_.get();
  base::FilePath stateful_path_;

  std::unique_ptr<MetricsReporter> reporter_;
};

TEST_F(MetricsReporterTest, ReportNBRComplete) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  reporter_ = std::make_unique<MetricsReporter>(
      std::move(metrics_library_mock_), temp_dir.GetPath());
  EXPECT_CALL(
      *metrics_library_mock_ptr_,
      SetOutputFile(temp_dir.GetPath().value() + "/" + kEventsFile.value()));
  EXPECT_CALL(*metrics_library_mock_ptr_,
              SendEnumToUMA(kRecoveryReason, kRecoveryReasonCode_NBR,
                            kRecoveryReasonCode_MAX));
  EXPECT_CALL(*metrics_library_mock_ptr_,
              SendToUMA(kRecoveryDurationMinutes, _, /*min=*/0,
                        kRecoveryDurationMinutes_MAX,
                        kRecoveryDurationMinutes_Buckets));
  reporter_->ReportNBRComplete();
}

TEST_F(MetricsReporterTest, ReportNBRCompleteFailToMountStateful) {
  reporter_ = std::make_unique<MetricsReporter>(
      std::move(metrics_library_mock_), base::FilePath{"/unmounted_dir"});
  reporter_->ReportNBRComplete();
}

}  // namespace minios
