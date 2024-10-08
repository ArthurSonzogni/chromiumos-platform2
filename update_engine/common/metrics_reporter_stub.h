// Copyright 2017 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_COMMON_METRICS_REPORTER_STUB_H_
#define UPDATE_ENGINE_COMMON_METRICS_REPORTER_STUB_H_

#include <string>

#include "update_engine/common/error_code.h"
#include "update_engine/common/metrics_constants.h"
#include "update_engine/common/metrics_reporter_interface.h"

namespace chromeos_update_engine {

class MetricsReporterStub : public MetricsReporterInterface {
 public:
  MetricsReporterStub() = default;
  MetricsReporterStub(const MetricsReporterStub&) = delete;
  MetricsReporterStub& operator=(const MetricsReporterStub&) = delete;

  ~MetricsReporterStub() override = default;

  void ReportRollbackMetrics(metrics::RollbackResult result) override {}

  void ReportEnterpriseRollbackMetrics(
      const std::string& metrics,
      const std::string& rollback_version) override {}

  void ReportDailyMetrics(base::TimeDelta os_age) override {}

  void ReportUpdateCheckMetrics(
      metrics::CheckResult result,
      metrics::CheckReaction reaction,
      metrics::DownloadErrorCode download_error_code) override {}

  void ReportUpdateAttemptMetrics(int attempt_number,
                                  PayloadType payload_type,
                                  base::TimeDelta duration,
                                  base::TimeDelta duration_uptime,
                                  int64_t payload_size,
                                  metrics::AttemptResult attempt_result,
                                  ErrorCode internal_error_code) override {}

  void ReportUpdateAttemptDownloadMetrics(
      int64_t payload_bytes_downloaded,
      int64_t payload_download_speed_bps,
      DownloadSource download_source,
      metrics::DownloadErrorCode payload_download_error_code,
      metrics::ConnectionType connection_type) override {}

  void ReportAbnormallyTerminatedUpdateAttemptMetrics() override {}

  void ReportSuccessfulUpdateMetrics(
      int attempt_count,
      int updates_abandoned_count,
      PayloadType payload_type,
      int64_t payload_size,
      int64_t num_bytes_downloaded[kNumDownloadSources],
      int download_overhead_percentage,
      base::TimeDelta total_duration,
      base::TimeDelta total_duration_uptime,
      int reboot_count,
      int url_switch_count) override {}

  void ReportCertificateCheckMetrics(ServerToCheck server_to_check,
                                     CertificateCheckResult result) override {}

  void ReportFailedUpdateCount(int target_attempt) override {}

  void ReportInvalidatedUpdate(bool success) override {}

  void ReportEnterpriseUpdateInvalidatedResult(bool success) override {}

  void ReportInstallDateProvisioningSource(int source, int max) override {}

  void ReportInternalErrorCode(ErrorCode error_code) override {}

  void ReportEnterpriseUpdateSeenToDownloadDays(
      bool has_time_restriction_policy, int time_to_update_days) override {}

  void ReportConsecutiveUpdateCount(int count) override {}

  void ReportFailedConsecutiveUpdate() override {}
};

}  // namespace chromeos_update_engine

#endif  // UPDATE_ENGINE_COMMON_METRICS_REPORTER_STUB_H_
