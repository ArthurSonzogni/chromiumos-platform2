// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRASH_REPORTER_SECURITY_ANOMALY_COLLECTOR_H_
#define CRASH_REPORTER_SECURITY_ANOMALY_COLLECTOR_H_

#include <map>
#include <memory>
#include <string>

#include <base/files/file_util.h>
#include <base/memory/ref_counted.h>
#include <base/memory/scoped_refptr.h>
#include <metrics/metrics_library.h>

#include "crash-reporter/crash_collector.h"

// Collector for processing security anomalies reported by secanomalyd.
class SecurityAnomalyCollector : public CrashCollector {
 public:
  explicit SecurityAnomalyCollector(
      const scoped_refptr<
          base::RefCountedData<std::unique_ptr<MetricsLibraryInterface>>>&
          metrics_lib);
  SecurityAnomalyCollector(const SecurityAnomalyCollector&) = delete;
  SecurityAnomalyCollector& operator=(const SecurityAnomalyCollector&) = delete;

  bool Collect(int32_t weight);

  static CollectorInfo GetHandlerInfo(
      int32_t weight,
      bool security_anomaly,
      const scoped_refptr<
          base::RefCountedData<std::unique_ptr<MetricsLibraryInterface>>>&
          metrics_lib);

 protected:
  void set_anomaly_report_path_for_testing(const base::FilePath& path) {
    anomaly_report_path_ = path;
  }

 private:
  friend class SecurityAnomalyCollectorTest;
  FRIEND_TEST(SecurityAnomalyCollectorTest, CollectOK);

  bool LoadSecurityAnomaly(std::string* content,
                           std::string* signature,
                           std::map<std::string, std::string>* extra_metadata);

  base::FilePath anomaly_report_path_;
};

#endif  // CRASH_REPORTER_SECURITY_ANOMALY_COLLECTOR_H_
