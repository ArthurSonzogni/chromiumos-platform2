// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// ArcvmKernelCollector handles crashes of the Linux kernel for ARCVM. When the
// ARCVM kernel crashes, it generates the crash log at
// /sys/fs/pstore/dmesg-ramoops-0 in ARCVM. The content of this file can be
// obtained from a ring buffer in /home/root/<hash>/crosvm/*.pstore file in
// ChromeOS. ArcvmKernelCollector receives the content of
// /sys/fs/pstore/dmesg-ramoops-0 and convert it for crash_sender.

#ifndef CRASH_REPORTER_ARCVM_KERNEL_COLLECTOR_H_
#define CRASH_REPORTER_ARCVM_KERNEL_COLLECTOR_H_

#include <memory>
#include <string>

#include <base/files/scoped_file.h>
#include <base/memory/ref_counted.h>
#include <base/memory/scoped_refptr.h>
#include <gtest/gtest_prod.h>  // for FRIEND_TEST
#include <metrics/metrics_library.h>

#include "crash-reporter/arc_util.h"
#include "crash-reporter/crash_collector.h"

// Collector for kernel crashes of ARCVM.
class ArcvmKernelCollector : public CrashCollector {
 public:
  explicit ArcvmKernelCollector(
      const scoped_refptr<
          base::RefCountedData<std::unique_ptr<MetricsLibraryInterface>>>&
          metrics_lib);
  ~ArcvmKernelCollector() override;

  // Handles a kernel crash of ARCVM.
  bool HandleCrash(const arc_util::BuildProperty& build_property);

  // Returns the severity level and product group of the crash.
  CrashCollector::ComputedCrashSeverity ComputeSeverity(
      const std::string& exec_name) override;

  static CollectorInfo GetHandlerInfo(
      bool arc_kernel,
      const arc_util::BuildProperty& build_property,
      const scoped_refptr<
          base::RefCountedData<std::unique_ptr<MetricsLibraryInterface>>>&
          metrics_lib);

 private:
  friend class ArcvmKernelCollectorTest;
  FRIEND_TEST(ArcvmKernelCollectorTest,
              HandleCrashWithRamoopsStreamAndTimestamp);
  FRIEND_TEST(ArcvmKernelCollectorTest, AddArcMetadata);

  // CrashCollector overrides.
  std::string GetProductVersion() const override;

  // Handles a kernel crash of ARCVM using the given stream for ramoops.
  // |timestamp| is used for a filename of the crash report.
  bool HandleCrashWithRamoopsStreamAndTimestamp(
      const arc_util::BuildProperty& build_property,
      FILE* ramoops_stream,
      time_t timestamp);

  // Adds ARC-related metadata to the crash report.
  void AddArcMetadata(const arc_util::BuildProperty& build_property);
};

#endif  // CRASH_REPORTER_ARCVM_KERNEL_COLLECTOR_H_
