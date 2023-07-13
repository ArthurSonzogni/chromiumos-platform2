// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crash-reporter/vm_collector.h"

#include <memory>

#include <base/memory/ref_counted.h>
#include <base/memory/scoped_refptr.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <metrics/metrics_library.h>
#include <metrics/metrics_library_mock.h>

namespace {

class VmCollectorTest : public ::testing::Test {
 public:
  VmCollectorTest()
      : collector_(
            base::MakeRefCounted<
                base::RefCountedData<std::unique_ptr<MetricsLibraryInterface>>>(
                std::make_unique<MetricsLibraryMock>())) {}

 protected:
  VmCollector collector_;
};

TEST_F(VmCollectorTest, ComputeSeverity) {
  CrashCollector::ComputedCrashSeverity computed_severity =
      collector_.ComputeSeverity("any executable");

  EXPECT_EQ(computed_severity.crash_severity,
            CrashCollector::CrashSeverity::kError);
  EXPECT_EQ(computed_severity.product_group,
            CrashCollector::Product::kPlatform);
}

}  // namespace
