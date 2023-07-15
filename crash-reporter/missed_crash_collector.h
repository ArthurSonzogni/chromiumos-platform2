// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRASH_REPORTER_MISSED_CRASH_COLLECTOR_H_
#define CRASH_REPORTER_MISSED_CRASH_COLLECTOR_H_

#include <stdint.h>
#include <stdio.h>

#include <memory>
#include <string>

#include <base/memory/ref_counted.h>
#include <base/memory/scoped_refptr.h>
#include <metrics/metrics_library.h>

#include "crash-reporter/crash_collector.h"

// Handles reports from anomaly_detector that we failed to capture a Chrome
// crash. The class is a bit of an oddity in that it doesn't collect its logs
// itself; instead, it has the logs passed to it on a file descriptor.
class MissedCrashCollector : public CrashCollector {
 public:
  explicit MissedCrashCollector(
      const scoped_refptr<
          base::RefCountedData<std::unique_ptr<MetricsLibraryInterface>>>&
          metrics_lib);
  ~MissedCrashCollector() override;

  bool Collect(int pid,
               int recent_miss_count,
               int recent_match_count,
               int pending_miss_count);

  // Does not take ownership.
  void set_input_file_for_testing(FILE* input_file) {
    input_file_ = input_file;
  }

  // Returns the severity level and product group of the crash.
  CrashCollector::ComputedCrashSeverity ComputeSeverity(
      const std::string& exec_name) override;

  static CollectorInfo GetHandlerInfo(
      bool missed_chrome_crash,
      int32_t pid,
      int32_t recent_miss_count,
      int32_t recent_match_count,
      int32_t pending_miss_count,
      const scoped_refptr<
          base::RefCountedData<std::unique_ptr<MetricsLibraryInterface>>>&
          metrics_lib);

 private:
  // FILE we can read from that contains the logs to attach to this crash
  // report. Default is stdin. Class does not own the FILE and will not close
  // it.
  FILE* input_file_;
};

#endif  // CRASH_REPORTER_MISSED_CRASH_COLLECTOR_H_
