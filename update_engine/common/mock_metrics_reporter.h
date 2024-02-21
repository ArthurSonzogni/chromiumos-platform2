// Copyright 2017 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_COMMON_MOCK_METRICS_REPORTER_H_
#define UPDATE_ENGINE_COMMON_MOCK_METRICS_REPORTER_H_

#include <string>

#include <gmock/gmock.h>

#include "update_engine/common/metrics_reporter_interface.h"

namespace chromeos_update_engine {

class MockMetricsReporter : public MetricsReporterInterface {
 public:
  MOCK_METHOD0(Initialize, void());

  MOCK_METHOD1(ReportRollbackMetrics, void(metrics::RollbackResult result));

  MOCK_METHOD2(ReportEnterpriseRollbackMetrics,
               void(const std::string& metric,
                    const std::string& rollback_version));

  MOCK_METHOD1(ReportDailyMetrics, void(base::TimeDelta os_age));

  MOCK_METHOD3(ReportUpdateCheckMetrics,
               void(metrics::CheckResult result,
                    metrics::CheckReaction reaction,
                    metrics::DownloadErrorCode download_error_code));

  MOCK_METHOD7(ReportUpdateAttemptMetrics,
               void(int attempt_number,
                    PayloadType payload_type,
                    base::TimeDelta duration,
                    base::TimeDelta duration_uptime,
                    int64_t payload_size,
                    metrics::AttemptResult attempt_result,
                    ErrorCode internal_error_code));

  MOCK_METHOD5(ReportUpdateAttemptDownloadMetrics,
               void(int64_t payload_bytes_downloaded,
                    int64_t payload_download_speed_bps,
                    DownloadSource download_source,
                    metrics::DownloadErrorCode payload_download_error_code,
                    metrics::ConnectionType connection_type));

  MOCK_METHOD0(ReportAbnormallyTerminatedUpdateAttemptMetrics, void());

  MOCK_METHOD10(ReportSuccessfulUpdateMetrics,
                void(int attempt_count,
                     int updates_abandoned_count,
                     PayloadType payload_type,
                     int64_t payload_size,
                     int64_t num_bytes_downloaded[kNumDownloadSources],
                     int download_overhead_percentage,
                     base::TimeDelta total_duration,
                     base::TimeDelta total_duration_uptime,
                     int reboot_count,
                     int url_switch_count));

  MOCK_METHOD2(ReportCertificateCheckMetrics,
               void(ServerToCheck server_to_check,
                    CertificateCheckResult result));

  MOCK_METHOD1(ReportFailedUpdateCount, void(int target_attempt));

  MOCK_METHOD1(ReportInvalidatedUpdate, void(bool success));

  MOCK_METHOD(void,
              ReportEnterpriseUpdateInvalidatedResult,
              (bool success),
              (override));

  MOCK_METHOD2(ReportInstallDateProvisioningSource, void(int source, int max));

  MOCK_METHOD1(ReportInternalErrorCode, void(ErrorCode error_code));

  MOCK_METHOD2(ReportEnterpriseUpdateSeenToDownloadDays,
               void(bool has_time_restriction_policy, int time_to_update_days));
  MOCK_METHOD(void, ReportConsecutiveUpdateCount, (int count), (override));
  MOCK_METHOD(void, ReportFailedConsecutiveUpdate, (), (override));
};

}  // namespace chromeos_update_engine

#endif  // UPDATE_ENGINE_COMMON_MOCK_METRICS_REPORTER_H_
