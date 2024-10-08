// Copyright 2017 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "update_engine/cros/metrics_reporter_omaha.h"

#include <memory>
#include <string>

#include <base/time/time.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <metrics/metrics_library_mock.h>

#include "update_engine/common/fake_clock.h"
#include "update_engine/cros/fake_system_state.h"

using base::TimeDelta;
using testing::_;
using testing::AnyNumber;
using testing::Return;

namespace chromeos_update_engine {
class MetricsReporterOmahaTest : public ::testing::Test {
 protected:
  MetricsReporterOmahaTest() = default;

  // Reset the metrics_lib_ to a mock library.
  void SetUp() override {
    FakeSystemState::CreateInstance();
    fake_clock_ = FakeSystemState::Get()->fake_clock();
    mock_metrics_lib_ = new testing::NiceMock<MetricsLibraryMock>();
    reporter_.metrics_lib_.reset(mock_metrics_lib_);
  }

  testing::NiceMock<MetricsLibraryMock>* mock_metrics_lib_;
  MetricsReporterOmaha reporter_;

  FakeClock* fake_clock_;
};

TEST_F(MetricsReporterOmahaTest, ReportDailyMetrics) {
  TimeDelta age = base::Days(10);
  EXPECT_CALL(*mock_metrics_lib_,
              SendToUMA(metrics::kMetricDailyOSAgeDays, _, _, _, _))
      .Times(1);

  reporter_.ReportDailyMetrics(age);
}

TEST_F(MetricsReporterOmahaTest, ReportUpdateCheckMetrics) {
  // We need to execute the report twice to test the time since last report.
  fake_clock_->SetWallclockTime(base::Time::FromInternalValue(1000000));
  fake_clock_->SetMonotonicTime(base::Time::FromInternalValue(1000000));

  metrics::CheckResult result = metrics::CheckResult::kUpdateAvailable;
  metrics::CheckReaction reaction = metrics::CheckReaction::kIgnored;
  metrics::DownloadErrorCode error_code =
      metrics::DownloadErrorCode::kHttpStatus200;

  EXPECT_CALL(*mock_metrics_lib_, SendEnumToUMA(metrics::kMetricCheckResult,
                                                static_cast<int>(result), _))
      .Times(2);
  EXPECT_CALL(*mock_metrics_lib_, SendEnumToUMA(metrics::kMetricCheckReaction,
                                                static_cast<int>(reaction), _))
      .Times(2);
  EXPECT_CALL(*mock_metrics_lib_,
              SendSparseToUMA(metrics::kMetricCheckDownloadErrorCode,
                              static_cast<int>(error_code)))
      .Times(2);

  // Not pinned nor rollback
  EXPECT_CALL(*mock_metrics_lib_,
              SendSparseToUMA(metrics::kMetricCheckTargetVersion, _))
      .Times(0);
  EXPECT_CALL(*mock_metrics_lib_,
              SendSparseToUMA(metrics::kMetricCheckRollbackTargetVersion, _))
      .Times(0);

  EXPECT_CALL(
      *mock_metrics_lib_,
      SendToUMA(metrics::kMetricCheckTimeSinceLastCheckMinutes, 1, _, _, _))
      .Times(1);
  EXPECT_CALL(*mock_metrics_lib_,
              SendToUMA(metrics::kMetricCheckTimeSinceLastCheckUptimeMinutes, 1,
                        _, _, _))
      .Times(1);

  reporter_.ReportUpdateCheckMetrics(result, reaction, error_code);

  // Advance the clock by 1 minute and report the same metrics again.
  fake_clock_->SetWallclockTime(base::Time::FromInternalValue(61000000));
  fake_clock_->SetMonotonicTime(base::Time::FromInternalValue(61000000));
  // Allow rollback
  reporter_.ReportUpdateCheckMetrics(result, reaction, error_code);
}

TEST_F(MetricsReporterOmahaTest, ReportUpdateCheckMetricsPinned) {
  OmahaRequestParams params;
  params.set_target_version_prefix("10575.");
  params.set_rollback_allowed(false);
  FakeSystemState::Get()->set_request_params(&params);

  metrics::CheckResult result = metrics::CheckResult::kUpdateAvailable;
  metrics::CheckReaction reaction = metrics::CheckReaction::kIgnored;
  metrics::DownloadErrorCode error_code =
      metrics::DownloadErrorCode::kHttpStatus200;

  EXPECT_CALL(*mock_metrics_lib_,
              SendSparseToUMA(metrics::kMetricCheckDownloadErrorCode, _));
  // Target version set, but not a rollback.
  EXPECT_CALL(*mock_metrics_lib_,
              SendSparseToUMA(metrics::kMetricCheckTargetVersion, 10575))
      .Times(1);
  EXPECT_CALL(*mock_metrics_lib_,
              SendSparseToUMA(metrics::kMetricCheckRollbackTargetVersion, _))
      .Times(0);

  reporter_.ReportUpdateCheckMetrics(result, reaction, error_code);
}

TEST_F(MetricsReporterOmahaTest, ReportUpdateCheckMetricsRollback) {
  OmahaRequestParams params;
  params.set_target_version_prefix("10575.");
  params.set_rollback_allowed(true);
  FakeSystemState::Get()->set_request_params(&params);

  metrics::CheckResult result = metrics::CheckResult::kUpdateAvailable;
  metrics::CheckReaction reaction = metrics::CheckReaction::kIgnored;
  metrics::DownloadErrorCode error_code =
      metrics::DownloadErrorCode::kHttpStatus200;

  EXPECT_CALL(*mock_metrics_lib_,
              SendSparseToUMA(metrics::kMetricCheckDownloadErrorCode, _));
  // Rollback.
  EXPECT_CALL(*mock_metrics_lib_,
              SendSparseToUMA(metrics::kMetricCheckTargetVersion, 10575))
      .Times(1);
  EXPECT_CALL(
      *mock_metrics_lib_,
      SendSparseToUMA(metrics::kMetricCheckRollbackTargetVersion, 10575))
      .Times(1);

  reporter_.ReportUpdateCheckMetrics(result, reaction, error_code);
}

TEST_F(MetricsReporterOmahaTest,
       ReportAbnormallyTerminatedUpdateAttemptMetrics) {
  EXPECT_CALL(
      *mock_metrics_lib_,
      SendEnumToUMA(
          metrics::kMetricAttemptResult,
          static_cast<int>(metrics::AttemptResult::kAbnormalTermination), _))
      .Times(1);

  reporter_.ReportAbnormallyTerminatedUpdateAttemptMetrics();
}

TEST_F(MetricsReporterOmahaTest, ReportUpdateAttemptMetrics) {
  fake_clock_->SetWallclockTime(base::Time::FromInternalValue(1000000));
  fake_clock_->SetMonotonicTime(base::Time::FromInternalValue(1000000));

  int attempt_number = 1;
  PayloadType payload_type = kPayloadTypeFull;
  TimeDelta duration = base::Minutes(1000);
  TimeDelta duration_uptime = base::Minutes(1000);

  int64_t payload_size = 100 * kNumBytesInOneMiB;

  metrics::AttemptResult attempt_result =
      metrics::AttemptResult::kInternalError;
  ErrorCode internal_error_code = ErrorCode::kDownloadInvalidMetadataSignature;

  EXPECT_CALL(*mock_metrics_lib_,
              SendToUMA(metrics::kMetricAttemptNumber, attempt_number, _, _, _))
      .Times(2);
  EXPECT_CALL(*mock_metrics_lib_,
              SendEnumToUMA(metrics::kMetricAttemptPayloadType,
                            static_cast<int>(payload_type), _))
      .Times(2);
  EXPECT_CALL(*mock_metrics_lib_,
              SendToUMA(metrics::kMetricAttemptDurationMinutes,
                        duration.InMinutes(), _, _, _))
      .Times(2);
  EXPECT_CALL(*mock_metrics_lib_,
              SendToUMA(metrics::kMetricAttemptDurationUptimeMinutes,
                        duration_uptime.InMinutes(), _, _, _))
      .Times(2);

  // Check the report of attempt result.
  EXPECT_CALL(*mock_metrics_lib_,
              SendEnumToUMA(metrics::kMetricAttemptResult,
                            static_cast<int>(attempt_result), _))
      .Times(2);
  EXPECT_CALL(*mock_metrics_lib_,
              SendEnumToUMA(metrics::kMetricAttemptInternalErrorCode,
                            static_cast<int>(internal_error_code), _))
      .Times(2);
  EXPECT_CALL(*mock_metrics_lib_,
              SendToUMA(metrics::kMetricAttemptPayloadSizeMiB, 100, _, _, _))
      .Times(2);

  // Check the duration between two reports.
  EXPECT_CALL(
      *mock_metrics_lib_,
      SendToUMA(metrics::kMetricAttemptTimeSinceLastAttemptMinutes, 1, _, _, _))
      .Times(1);
  EXPECT_CALL(
      *mock_metrics_lib_,
      SendToUMA(metrics::kMetricAttemptTimeSinceLastAttemptUptimeMinutes, 1, _,
                _, _))
      .Times(1);

  reporter_.ReportUpdateAttemptMetrics(attempt_number, payload_type, duration,
                                       duration_uptime, payload_size,
                                       attempt_result, internal_error_code);

  // Advance the clock by 1 minute and report the same metrics again.
  fake_clock_->SetWallclockTime(base::Time::FromInternalValue(61000000));
  fake_clock_->SetMonotonicTime(base::Time::FromInternalValue(61000000));
  reporter_.ReportUpdateAttemptMetrics(attempt_number, payload_type, duration,
                                       duration_uptime, payload_size,
                                       attempt_result, internal_error_code);
}

TEST_F(MetricsReporterOmahaTest, ReportUpdateAttemptDownloadMetrics) {
  int64_t payload_bytes_downloaded = 200 * kNumBytesInOneMiB;
  int64_t payload_download_speed_bps = 100 * 1000;
  DownloadSource download_source = kDownloadSourceHttpServer;
  metrics::DownloadErrorCode payload_download_error_code =
      metrics::DownloadErrorCode::kDownloadError;
  metrics::ConnectionType connection_type = metrics::ConnectionType::kCellular;

  EXPECT_CALL(
      *mock_metrics_lib_,
      SendToUMA(metrics::kMetricAttemptPayloadBytesDownloadedMiB, 200, _, _, _))
      .Times(1);
  EXPECT_CALL(
      *mock_metrics_lib_,
      SendToUMA(metrics::kMetricAttemptPayloadDownloadSpeedKBps, 100, _, _, _))
      .Times(1);
  EXPECT_CALL(*mock_metrics_lib_,
              SendEnumToUMA(metrics::kMetricAttemptDownloadSource,
                            static_cast<int>(download_source), _))
      .Times(1);
  EXPECT_CALL(*mock_metrics_lib_,
              SendSparseToUMA(metrics::kMetricAttemptDownloadErrorCode,
                              static_cast<int>(payload_download_error_code)))
      .Times(1);
  EXPECT_CALL(*mock_metrics_lib_,
              SendEnumToUMA(metrics::kMetricAttemptConnectionType,
                            static_cast<int>(connection_type), _))
      .Times(1);

  reporter_.ReportUpdateAttemptDownloadMetrics(
      payload_bytes_downloaded, payload_download_speed_bps, download_source,
      payload_download_error_code, connection_type);
}

TEST_F(MetricsReporterOmahaTest, ReportSuccessfulUpdateMetrics) {
  int attempt_count = 3;
  int updates_abandoned_count = 2;
  PayloadType payload_type = kPayloadTypeDelta;
  int64_t payload_size = 200 * kNumBytesInOneMiB;
  int64_t num_bytes_downloaded[kNumDownloadSources] = {};
  // 200MiB payload downloaded from HttpsServer.
  num_bytes_downloaded[0] = 200 * kNumBytesInOneMiB;
  int download_overhead_percentage = 20;
  TimeDelta total_duration = base::Minutes(30);
  TimeDelta total_duration_uptime = base::Minutes(20);
  int reboot_count = 2;
  int url_switch_count = 2;

  EXPECT_CALL(
      *mock_metrics_lib_,
      SendToUMA(metrics::kMetricSuccessfulUpdatePayloadSizeMiB, 200, _, _, _))
      .Times(1);

  // Check the report to both BytesDownloadedMiBHttpsServer and
  // BytesDownloadedMiB
  std::string DownloadedMiBMetric =
      metrics::kMetricSuccessfulUpdateBytesDownloadedMiB;
  DownloadedMiBMetric += "HttpsServer";
  EXPECT_CALL(*mock_metrics_lib_, SendToUMA(DownloadedMiBMetric, 200, _, _, _))
      .Times(1);
  EXPECT_CALL(*mock_metrics_lib_,
              SendToUMA(metrics::kMetricSuccessfulUpdateBytesDownloadedMiB, 200,
                        _, _, _))
      .Times(1);

  EXPECT_CALL(*mock_metrics_lib_,
              SendToUMA(metrics::kMetricSuccessfulUpdateDownloadSourcesUsed, 1,
                        _, _, _))
      .Times(1);
  EXPECT_CALL(
      *mock_metrics_lib_,
      SendToUMA(metrics::kMetricSuccessfulUpdateDownloadOverheadPercentage, 20,
                _, _, _));

  EXPECT_CALL(*mock_metrics_lib_,
              SendToUMA(metrics::kMetricSuccessfulUpdateUrlSwitchCount,
                        url_switch_count, _, _, _))
      .Times(1);
  EXPECT_CALL(*mock_metrics_lib_,
              SendToUMA(metrics::kMetricSuccessfulUpdateTotalDurationMinutes,
                        30, _, _, _))
      .Times(1);
  EXPECT_CALL(
      *mock_metrics_lib_,
      SendToUMA(metrics::kMetricSuccessfulUpdateTotalDurationUptimeMinutes, 20,
                _, _, _))
      .Times(1);
  EXPECT_CALL(*mock_metrics_lib_,
              SendToUMA(metrics::kMetricSuccessfulUpdateRebootCount,
                        reboot_count, _, _, _))
      .Times(1);
  EXPECT_CALL(*mock_metrics_lib_,
              SendEnumToUMA(metrics::kMetricSuccessfulUpdatePayloadType,
                            payload_type, _))
      .Times(1);
  EXPECT_CALL(*mock_metrics_lib_,
              SendToUMA(metrics::kMetricSuccessfulUpdateAttemptCount,
                        attempt_count, _, _, _))
      .Times(1);
  EXPECT_CALL(*mock_metrics_lib_,
              SendToUMA(metrics::kMetricSuccessfulUpdateUpdatesAbandonedCount,
                        updates_abandoned_count, _, _, _))
      .Times(1);

  reporter_.ReportSuccessfulUpdateMetrics(
      attempt_count, updates_abandoned_count, payload_type, payload_size,
      num_bytes_downloaded, download_overhead_percentage, total_duration,
      total_duration_uptime, reboot_count, url_switch_count);
}

TEST_F(MetricsReporterOmahaTest, ReportRollbackMetrics) {
  metrics::RollbackResult result = metrics::RollbackResult::kSuccess;
  EXPECT_CALL(*mock_metrics_lib_, SendEnumToUMA(metrics::kMetricRollbackResult,
                                                static_cast<int>(result), _))
      .Times(1);

  reporter_.ReportRollbackMetrics(result);
}

TEST_F(MetricsReporterOmahaTest, ReportEnterpriseRollbackMetrics) {
  EXPECT_CALL(*mock_metrics_lib_,
              SendSparseToUMA(metrics::kMetricEnterpriseRollbackSuccess, 10575))
      .Times(1);
  reporter_.ReportEnterpriseRollbackMetrics(
      metrics::kMetricEnterpriseRollbackSuccess, "10575.39.2");

  EXPECT_CALL(*mock_metrics_lib_,
              SendSparseToUMA(metrics::kMetricEnterpriseRollbackFailure, 10323))
      .Times(1);
  reporter_.ReportEnterpriseRollbackMetrics(
      metrics::kMetricEnterpriseRollbackFailure, "10323.67.7");

  EXPECT_CALL(
      *mock_metrics_lib_,
      SendSparseToUMA(metrics::kMetricEnterpriseRollbackBlockedByFSI, 10324))
      .Times(1);
  reporter_.ReportEnterpriseRollbackMetrics(
      metrics::kMetricEnterpriseRollbackBlockedByFSI, "10324.63.0");
}

TEST_F(MetricsReporterOmahaTest, ReportCertificateCheckMetrics) {
  ServerToCheck server_to_check = ServerToCheck::kUpdate;
  CertificateCheckResult result = CertificateCheckResult::kValid;
  EXPECT_CALL(*mock_metrics_lib_,
              SendEnumToUMA(metrics::kMetricCertificateCheckUpdateCheck,
                            static_cast<int>(result), _))
      .Times(1);

  reporter_.ReportCertificateCheckMetrics(server_to_check, result);
}

TEST_F(MetricsReporterOmahaTest, ReportFailedUpdateCount) {
  int target_attempt = 3;
  EXPECT_CALL(*mock_metrics_lib_, SendToUMA(metrics::kMetricFailedUpdateCount,
                                            target_attempt, _, _, _))
      .Times(1);

  reporter_.ReportFailedUpdateCount(target_attempt);
}

TEST_F(MetricsReporterOmahaTest, ReportInvalidatedUpdateSuccess) {
  EXPECT_CALL(*mock_metrics_lib_,
              SendBoolToUMA(metrics::kMetricInvalidatedUpdate, true))
      .Times(1);

  reporter_.ReportInvalidatedUpdate(true);
}

TEST_F(MetricsReporterOmahaTest, ReportInvalidatedUpdateFailure) {
  EXPECT_CALL(*mock_metrics_lib_,
              SendBoolToUMA(metrics::kMetricInvalidatedUpdate, false))
      .Times(1);

  reporter_.ReportInvalidatedUpdate(false);
}

TEST_F(MetricsReporterOmahaTest,
       ReportEnterpriseUpdateInvalidatedResultSuccess) {
  EXPECT_CALL(
      *mock_metrics_lib_,
      SendBoolToUMA(metrics::kMetricEnterpriseUpdateInvalidatedResult, true))
      .Times(1);

  reporter_.ReportEnterpriseUpdateInvalidatedResult(true);
}

TEST_F(MetricsReporterOmahaTest,
       ReportEnterpriseUpdateInvalidatedResultFailure) {
  EXPECT_CALL(
      *mock_metrics_lib_,
      SendBoolToUMA(metrics::kMetricEnterpriseUpdateInvalidatedResult, false))
      .Times(1);

  reporter_.ReportEnterpriseUpdateInvalidatedResult(false);
}

TEST_F(MetricsReporterOmahaTest, ReportInstallDateProvisioningSource) {
  int source = 2;
  int max = 5;
  EXPECT_CALL(
      *mock_metrics_lib_,
      SendEnumToUMA(metrics::kMetricInstallDateProvisioningSource, source, max))
      .Times(1);

  reporter_.ReportInstallDateProvisioningSource(source, max);
}

TEST_F(MetricsReporterOmahaTest, ReportConsecutiveUpdateCount) {
  int consecutive_update_count = 2;
  EXPECT_CALL(*mock_metrics_lib_,
              SendSparseToUMA(metrics::kMetricConsecutiveUpdateCount,
                              consecutive_update_count));

  reporter_.ReportConsecutiveUpdateCount(consecutive_update_count);
}

TEST_F(MetricsReporterOmahaTest, ReportFailedConsecutiveUpdate) {
  EXPECT_CALL(*mock_metrics_lib_,
              SendBoolToUMA(metrics::kMetricConsecutiveUpdateFailed, true));

  reporter_.ReportFailedConsecutiveUpdate();
}

TEST_F(MetricsReporterOmahaTest, WallclockDurationHelper) {
  base::TimeDelta duration;
  const std::string state_variable_key = "test-prefs";

  // Initialize wallclock to 1 sec.
  fake_clock_->SetWallclockTime(base::Time::FromInternalValue(1000000));

  // First time called so no previous measurement available.
  EXPECT_FALSE(
      reporter_.WallclockDurationHelper(state_variable_key, &duration));

  // Next time, we should get zero since the clock didn't advance.
  EXPECT_TRUE(reporter_.WallclockDurationHelper(state_variable_key, &duration));
  EXPECT_EQ(duration.InSeconds(), 0);

  // We can also call it as many times as we want with it being
  // considered a failure.
  EXPECT_TRUE(reporter_.WallclockDurationHelper(state_variable_key, &duration));
  EXPECT_EQ(duration.InSeconds(), 0);
  EXPECT_TRUE(reporter_.WallclockDurationHelper(state_variable_key, &duration));
  EXPECT_EQ(duration.InSeconds(), 0);

  // Advance the clock one second, then we should get 1 sec on the
  // next call and 0 sec on the subsequent call.
  fake_clock_->SetWallclockTime(base::Time::FromInternalValue(2000000));
  EXPECT_TRUE(reporter_.WallclockDurationHelper(state_variable_key, &duration));
  EXPECT_EQ(duration.InSeconds(), 1);
  EXPECT_TRUE(reporter_.WallclockDurationHelper(state_variable_key, &duration));
  EXPECT_EQ(duration.InSeconds(), 0);

  // Advance clock two seconds and we should get 2 sec and then 0 sec.
  fake_clock_->SetWallclockTime(base::Time::FromInternalValue(4000000));
  EXPECT_TRUE(reporter_.WallclockDurationHelper(state_variable_key, &duration));
  EXPECT_EQ(duration.InSeconds(), 2);
  EXPECT_TRUE(reporter_.WallclockDurationHelper(state_variable_key, &duration));
  EXPECT_EQ(duration.InSeconds(), 0);

  // There's a possibility that the wallclock can go backwards (NTP
  // adjustments, for example) so check that we properly handle this
  // case.
  fake_clock_->SetWallclockTime(base::Time::FromInternalValue(3000000));
  EXPECT_FALSE(
      reporter_.WallclockDurationHelper(state_variable_key, &duration));
  fake_clock_->SetWallclockTime(base::Time::FromInternalValue(4000000));
  EXPECT_TRUE(reporter_.WallclockDurationHelper(state_variable_key, &duration));
  EXPECT_EQ(duration.InSeconds(), 1);
}

TEST_F(MetricsReporterOmahaTest, MonotonicDurationHelper) {
  int64_t storage = 0;
  base::TimeDelta duration;

  // Initialize monotonic clock to 1 sec.
  fake_clock_->SetMonotonicTime(base::Time::FromInternalValue(1000000));

  // First time called so no previous measurement available.
  EXPECT_FALSE(reporter_.MonotonicDurationHelper(&storage, &duration));

  // Next time, we should get zero since the clock didn't advance.
  EXPECT_TRUE(reporter_.MonotonicDurationHelper(&storage, &duration));
  EXPECT_EQ(duration.InSeconds(), 0);

  // We can also call it as many times as we want with it being
  // considered a failure.
  EXPECT_TRUE(reporter_.MonotonicDurationHelper(&storage, &duration));
  EXPECT_EQ(duration.InSeconds(), 0);
  EXPECT_TRUE(reporter_.MonotonicDurationHelper(&storage, &duration));
  EXPECT_EQ(duration.InSeconds(), 0);

  // Advance the clock one second, then we should get 1 sec on the
  // next call and 0 sec on the subsequent call.
  fake_clock_->SetMonotonicTime(base::Time::FromInternalValue(2000000));
  EXPECT_TRUE(reporter_.MonotonicDurationHelper(&storage, &duration));
  EXPECT_EQ(duration.InSeconds(), 1);
  EXPECT_TRUE(reporter_.MonotonicDurationHelper(&storage, &duration));
  EXPECT_EQ(duration.InSeconds(), 0);

  // Advance clock two seconds and we should get 2 sec and then 0 sec.
  fake_clock_->SetMonotonicTime(base::Time::FromInternalValue(4000000));
  EXPECT_TRUE(reporter_.MonotonicDurationHelper(&storage, &duration));
  EXPECT_EQ(duration.InSeconds(), 2);
  EXPECT_TRUE(reporter_.MonotonicDurationHelper(&storage, &duration));
  EXPECT_EQ(duration.InSeconds(), 0);
}

}  // namespace chromeos_update_engine
