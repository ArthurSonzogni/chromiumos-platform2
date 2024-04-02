// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "regmon/metrics/metrics_reporter_impl.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "metrics/fake_metrics_library.h"

namespace regmon::metrics {

using ::testing::ElementsAre;

class MetricsReporterImplTest : public ::testing::Test {
 public:
  MetricsReporterImplTest()
      : metrics_reporter_(std::make_unique<MetricsReporterImpl>(metrics_lib_)) {
  }

  FakeMetricsLibrary& lib() { return metrics_lib_; }

 protected:
  std::unique_ptr<MetricsReporterImpl> metrics_reporter_;

 private:
  FakeMetricsLibrary metrics_lib_;
};

TEST_F(MetricsReporterImplTest, ReportAnnotationViolationFakeReturnsTrue) {
  EXPECT_TRUE(metrics_reporter_->ReportAnnotationViolation(11111));
}

TEST_F(MetricsReporterImplTest, ReportAnnotationViolationCallsUma) {
  metrics_reporter_->ReportAnnotationViolation(11111);
  metrics_reporter_->ReportAnnotationViolation(22222);

  EXPECT_EQ(lib().NumCalls("NetworkAnnotationMonitor.PolicyViolation"), 2);
  EXPECT_THAT(lib().GetCalls("NetworkAnnotationMonitor.PolicyViolation"),
              ElementsAre(11111, 22222));
}

TEST_F(MetricsReporterImplTest, ReportAnnotationViolationOrder) {
  metrics_reporter_->ReportAnnotationViolation(11111);
  metrics_reporter_->ReportAnnotationViolation(22222);

  EXPECT_EQ(lib().GetLast("NetworkAnnotationMonitor.PolicyViolation"), 22222);
}

TEST_F(MetricsReporterImplTest, ReportAnnotationViolationNotCalled) {
  EXPECT_EQ(lib().GetLast("NetworkAnnotationMonitor.PolicyViolation"),
            FakeMetricsLibrary::kInvalid);
}

}  // namespace regmon::metrics
