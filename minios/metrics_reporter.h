// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MINIOS_METRICS_REPORTER_H_
#define MINIOS_METRICS_REPORTER_H_

#include <memory>

#include <base/files/file_path.h>
#include <base/time/time.h>
#include <metrics/metrics_library.h>

#include "minios/utils.h"

namespace minios {

extern const char kRecoveryDurationMinutes[];
extern const char kRecoveryReason[];
// Metrics file path in the stateful partition. See:
// init/upstart/send-recovery-metrics.conf
extern const base::FilePath kEventsFile;

extern const int kRecoveryDurationMinutes_Buckets;
extern const int kRecoveryDurationMinutes_MAX;
extern const int kRecoveryReasonCode_NBR;
extern const int kRecoveryReasonCode_MAX;

class MetricsReporterInterface {
 public:
  virtual ~MetricsReporterInterface() = default;

  // Record the NBR start time.
  virtual void RecordNBRStart() = 0;

  // Report the recovery reason to be NBR, and the duration for it to complete.
  // The metrics will be written to the stateful partition to be reported after
  // next boot. See: init/upstart/send-recovery-metrics.conf
  virtual void ReportNBRComplete() = 0;
};

class MetricsReporter : public MetricsReporterInterface {
 public:
  explicit MetricsReporter(
      std::unique_ptr<MetricsLibraryInterface> metrics_lib = nullptr,
      const base::FilePath& stateful_path = kStatefulPath);
  virtual ~MetricsReporter() = default;

  MetricsReporter(const MetricsReporter&) = delete;
  MetricsReporter& operator=(const MetricsReporter&) = delete;

  void RecordNBRStart() override;
  void ReportNBRComplete() override;

 private:
  std::unique_ptr<MetricsLibraryInterface> metrics_lib_;
  base::FilePath stateful_path_;

  base::Time start_time_;
};

}  // namespace minios

#endif  // MINIOS_METRICS_REPORTER_H_
