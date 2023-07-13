// Copyright 2014 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRASH_REPORTER_KERNEL_COLLECTOR_TEST_H_
#define CRASH_REPORTER_KERNEL_COLLECTOR_TEST_H_

#include "crash-reporter/kernel_collector.h"

#include <memory>

#include <base/memory/ref_counted.h>
#include <base/memory/scoped_refptr.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <metrics/metrics_library.h>
#include <metrics/metrics_library_mock.h>

class KernelCollectorMock : public KernelCollector {
 public:
  KernelCollectorMock()
      : KernelCollector(
            base::MakeRefCounted<
                base::RefCountedData<std::unique_ptr<MetricsLibraryInterface>>>(
                std::make_unique<MetricsLibraryMock>())) {}
  MOCK_METHOD(bool, DumpDirMounted, (), (override));
  MOCK_METHOD(void, SetUpDBus, (), (override));
};

#endif  // CRASH_REPORTER_KERNEL_COLLECTOR_TEST_H_
