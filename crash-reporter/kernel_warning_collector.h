// Copyright 2013 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The kernel warning collector gathers logs from kernel warnings.
// Anomaly detector runs the kernel warning collector when it detects strings
// matching the expected warning pattern in /var/log/messages.

#ifndef CRASH_REPORTER_KERNEL_WARNING_COLLECTOR_H_
#define CRASH_REPORTER_KERNEL_WARNING_COLLECTOR_H_

#include <memory>
#include <string>

#include <base/memory/ref_counted.h>
#include <base/memory/scoped_refptr.h>
#include <gtest/gtest_prod.h>  // for FRIEND_TEST
#include <metrics/metrics_library.h>

#include "crash-reporter/crash_collector.h"

// Kernel warning collector.
class KernelWarningCollector : public CrashCollector {
 public:
  enum WarningType {
    kGeneric,
    kWifi,
    kKfence,
    kSMMUFault,
    kSuspend,
    // Iwlwifi is the name of Intel WiFi driver that we want to collect its
    // error dumps.
    kIwlwifi,
    // Ath10k is the name of Qualcomm WiFi driver that we want to collect its
    // error dumps.
    kAth10k,
    // Ath11k is the name of Qualcomm WiFi driver that we want to collect its
    // error dumps.
    kAth11k,
  };

  explicit KernelWarningCollector(
      const scoped_refptr<
          base::RefCountedData<std::unique_ptr<MetricsLibraryInterface>>>&
          metrics_lib);
  KernelWarningCollector(const KernelWarningCollector&) = delete;
  KernelWarningCollector& operator=(const KernelWarningCollector&) = delete;

  ~KernelWarningCollector() override;

  // Collects warning.
  bool Collect(int weight, WarningType type);

  // Returns the severity level and product group of the crash.
  CrashCollector::ComputedCrashSeverity ComputeSeverity(
      const std::string& exec_name) override;

  static CollectorInfo GetHandlerInfo(
      int32_t weight,
      bool kernel_warning,
      bool kernel_wifi_warning,
      bool kernel_kfence,
      bool kernel_smmu_fault,
      bool kernel_suspend_warning,
      bool kernel_iwlwifi_error,
      bool kernel_ath10k_error,
      bool kernel_ath11k_error,
      const scoped_refptr<
          base::RefCountedData<std::unique_ptr<MetricsLibraryInterface>>>&
          metrics_lib);

 protected:
  std::string warning_report_path_;

 private:
  friend class KernelWarningCollectorTest;
  FRIEND_TEST(KernelWarningCollectorTest, CollectOK);

  // Reads the full content of the kernel warn dump and its signature.
  bool LoadKernelWarning(std::string* content,
                         std::string* signature,
                         std::string* func_name,
                         WarningType type);
  bool ExtractSignature(const std::string& content,
                        std::string* signature,
                        std::string* func_name);
  bool ExtractIwlwifiSignature(const std::string& content,
                               std::string* signature,
                               std::string* func_name);
  bool ExtractKfenceSignature(const std::string& content,
                              std::string* signature,
                              std::string* func_name);
  bool ExtractSMMUFaultSignature(const std::string& content,
                                 std::string* signature,
                                 std::string* func_name);
  bool ExtractAth10kSignature(const std::string& content,
                              std::string* signature,
                              std::string* func_name);
  bool ExtractAth11kSignature(const std::string& content,
                              std::string* signature,
                              std::string* func_name);
};

#endif  // CRASH_REPORTER_KERNEL_WARNING_COLLECTOR_H_
