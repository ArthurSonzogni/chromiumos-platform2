// Copyright 2014 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "update_engine/cros/metrics_reporter_omaha.h"

#include <memory>

#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <metrics/metrics_library.h>

#include "update_engine/common/constants.h"
#include "update_engine/common/prefs_interface.h"
#include "update_engine/common/system_state.h"
#include "update_engine/common/utils.h"
#include "update_engine/cros/omaha_request_params.h"
#include "update_engine/metrics_utils.h"

using base::Time;
using base::TimeDelta;
using std::string;

namespace chromeos_update_engine {
namespace metrics {

// UpdateEngine.Daily.* metrics.
const char kMetricDailyOSAgeDays[] = "UpdateEngine.Daily.OSAgeDays";

// UpdateEngine.Check.* metrics.
const char kMetricCheckDownloadErrorCode[] =
    "UpdateEngine.Check.DownloadErrorCode";
const char kMetricCheckReaction[] = "UpdateEngine.Check.Reaction";
const char kMetricCheckResult[] = "UpdateEngine.Check.Result";
const char kMetricCheckTargetVersion[] = "UpdateEngine.Check.TargetVersion";
const char kMetricCheckRollbackTargetVersion[] =
    "UpdateEngine.Check.RollbackTargetVersion";
const char kMetricCheckTimeSinceLastCheckMinutes[] =
    "UpdateEngine.Check.TimeSinceLastCheckMinutes";
const char kMetricCheckTimeSinceLastCheckUptimeMinutes[] =
    "UpdateEngine.Check.TimeSinceLastCheckUptimeMinutes";

// UpdateEngine.Attempt.* metrics.
const char kMetricAttemptNumber[] = "UpdateEngine.Attempt.Number";
const char kMetricAttemptPayloadType[] = "UpdateEngine.Attempt.PayloadType";
const char kMetricAttemptPayloadSizeMiB[] =
    "UpdateEngine.Attempt.PayloadSizeMiB";
const char kMetricAttemptConnectionType[] =
    "UpdateEngine.Attempt.ConnectionType";
const char kMetricAttemptDurationMinutes[] =
    "UpdateEngine.Attempt.DurationMinutes";
const char kMetricAttemptDurationUptimeMinutes[] =
    "UpdateEngine.Attempt.DurationUptimeMinutes";
const char kMetricAttemptTimeSinceLastAttemptMinutes[] =
    "UpdateEngine.Attempt.TimeSinceLastAttemptMinutes";
const char kMetricAttemptTimeSinceLastAttemptUptimeMinutes[] =
    "UpdateEngine.Attempt.TimeSinceLastAttemptUptimeMinutes";
const char kMetricAttemptPayloadBytesDownloadedMiB[] =
    "UpdateEngine.Attempt.PayloadBytesDownloadedMiB";
const char kMetricAttemptPayloadDownloadSpeedKBps[] =
    "UpdateEngine.Attempt.PayloadDownloadSpeedKBps";
const char kMetricAttemptDownloadSource[] =
    "UpdateEngine.Attempt.DownloadSource";
const char kMetricAttemptResult[] = "UpdateEngine.Attempt.Result";
const char kMetricAttemptInternalErrorCode[] =
    "UpdateEngine.Attempt.InternalErrorCode";
const char kMetricAttemptDownloadErrorCode[] =
    "UpdateEngine.Attempt.DownloadErrorCode";

// UpdateEngine.SuccessfulUpdate.* metrics.
const char kMetricSuccessfulUpdateAttemptCount[] =
    "UpdateEngine.SuccessfulUpdate.AttemptCount";
const char kMetricSuccessfulUpdateBytesDownloadedMiB[] =
    "UpdateEngine.SuccessfulUpdate.BytesDownloadedMiB";
const char kMetricSuccessfulUpdateDownloadOverheadPercentage[] =
    "UpdateEngine.SuccessfulUpdate.DownloadOverheadPercentage";
const char kMetricSuccessfulUpdateDownloadSourcesUsed[] =
    "UpdateEngine.SuccessfulUpdate.DownloadSourcesUsed";
const char kMetricSuccessfulUpdatePayloadType[] =
    "UpdateEngine.SuccessfulUpdate.PayloadType";
const char kMetricSuccessfulUpdatePayloadSizeMiB[] =
    "UpdateEngine.SuccessfulUpdate.PayloadSizeMiB";
const char kMetricSuccessfulUpdateRebootCount[] =
    "UpdateEngine.SuccessfulUpdate.RebootCount";
const char kMetricSuccessfulUpdateTotalDurationMinutes[] =
    "UpdateEngine.SuccessfulUpdate.TotalDurationMinutes";
const char kMetricSuccessfulUpdateTotalDurationUptimeMinutes[] =
    "UpdateEngine.SuccessfulUpdate.TotalDurationUptimeMinutes";
const char kMetricSuccessfulUpdateUpdatesAbandonedCount[] =
    "UpdateEngine.SuccessfulUpdate.UpdatesAbandonedCount";
const char kMetricSuccessfulUpdateUrlSwitchCount[] =
    "UpdateEngine.SuccessfulUpdate.UrlSwitchCount";

// UpdateEngine.Rollback.* metric.
const char kMetricRollbackResult[] = "UpdateEngine.Rollback.Result";

// UpdateEngine.EnterpriseRollback.* metrics.
const char kMetricEnterpriseRollbackFailure[] =
    "UpdateEngine.EnterpriseRollback.Failure";
const char kMetricEnterpriseRollbackSuccess[] =
    "UpdateEngine.EnterpriseRollback.Success";
extern const char kMetricEnterpriseRollbackBlockedByFSI[] =
    "UpdateEngine.EnterpriseRollback.BlockedByFSI";

// UpdateEngine.CertificateCheck.* metrics.
const char kMetricCertificateCheckUpdateCheck[] =
    "UpdateEngine.CertificateCheck.UpdateCheck";
const char kMetricCertificateCheckDownload[] =
    "UpdateEngine.CertificateCheck.Download";

// UpdateEngine.* metrics.
const char kMetricEnterpriseUpdateInvalidatedResult[] =
    "UpdateEngine.EnterpriseUpdateInvalidatedResult";
const char kMetricFailedUpdateCount[] = "UpdateEngine.FailedUpdateCount";
const char kMetricInstallDateProvisioningSource[] =
    "UpdateEngine.InstallDateProvisioningSource";
const char kMetricInvalidatedUpdate[] = "UpdateEngine.UpdateInvalidated";

// UpdateEngine.ConsecutiveUpdate.* metrics.
const char kMetricConsecutiveUpdateCount[] =
    "UpdateEngine.ConsecutiveUpdate.Count";
const char kMetricConsecutiveUpdateFailed[] =
    "UpdateEngine.ConsecutiveUpdate.Failed";

std::unique_ptr<MetricsReporterInterface> CreateMetricsReporter() {
  return std::make_unique<MetricsReporterOmaha>();
}

}  // namespace metrics

MetricsReporterOmaha::MetricsReporterOmaha()
    : metrics_lib_(new MetricsLibrary()) {}

void MetricsReporterOmaha::ReportDailyMetrics(base::TimeDelta os_age) {
  string metric = metrics::kMetricDailyOSAgeDays;
  metrics_lib_->SendToUMA(metric, static_cast<int>(os_age.InDays()),
                          0,       // min: 0 days
                          6 * 30,  // max: 6 months (approx)
                          50);     // num_buckets
}

void MetricsReporterOmaha::ReportUpdateCheckMetrics(
    metrics::CheckResult result,
    metrics::CheckReaction reaction,
    metrics::DownloadErrorCode download_error_code) {
  string metric;
  int value;
  int exclusive_max;

  if (result != metrics::CheckResult::kUnset) {
    metric = metrics::kMetricCheckResult;
    value = static_cast<int>(result);
    exclusive_max = static_cast<int>(metrics::CheckResult::kNumConstants);
    metrics_lib_->SendEnumToUMA(metric, value, exclusive_max);
  }
  if (reaction != metrics::CheckReaction::kUnset) {
    metric = metrics::kMetricCheckReaction;
    value = static_cast<int>(reaction);
    exclusive_max = static_cast<int>(metrics::CheckReaction::kNumConstants);
    metrics_lib_->SendEnumToUMA(metric, value, exclusive_max);
  }
  if (download_error_code != metrics::DownloadErrorCode::kUnset) {
    metric = metrics::kMetricCheckDownloadErrorCode;
    value = static_cast<int>(download_error_code);
    metrics_lib_->SendSparseToUMA(metric, value);
  }

  base::TimeDelta time_since_last;
  if (WallclockDurationHelper(kPrefsMetricsCheckLastReportingTime,
                              &time_since_last)) {
    metric = metrics::kMetricCheckTimeSinceLastCheckMinutes;
    metrics_lib_->SendToUMA(metric, time_since_last.InMinutes(),
                            0,             // min: 0 min
                            30 * 24 * 60,  // max: 30 days
                            50);           // num_buckets
  }

  base::TimeDelta uptime_since_last;
  static int64_t uptime_since_last_storage = 0;
  if (MonotonicDurationHelper(&uptime_since_last_storage, &uptime_since_last)) {
    metric = metrics::kMetricCheckTimeSinceLastCheckUptimeMinutes;
    metrics_lib_->SendToUMA(metric, uptime_since_last.InMinutes(),
                            0,             // min: 0 min
                            30 * 24 * 60,  // max: 30 days
                            50);           // num_buckets
  }

  // First section of target version specified for the update.
  if (SystemState::Get()->request_params()) {
    string target_version =
        SystemState::Get()->request_params()->target_version_prefix();
    value = utils::VersionPrefix(target_version);
    if (value != 0) {
      metric = metrics::kMetricCheckTargetVersion;
      metrics_lib_->SendSparseToUMA(metric, value);
      if (SystemState::Get()->request_params()->rollback_allowed()) {
        metric = metrics::kMetricCheckRollbackTargetVersion;
        metrics_lib_->SendSparseToUMA(metric, value);
      }
    }
  }
}

void MetricsReporterOmaha::ReportAbnormallyTerminatedUpdateAttemptMetrics() {
  string metric = metrics::kMetricAttemptResult;
  metrics::AttemptResult attempt_result =
      metrics::AttemptResult::kAbnormalTermination;

  metrics_lib_->SendEnumToUMA(
      metric, static_cast<int>(attempt_result),
      static_cast<int>(metrics::AttemptResult::kNumConstants));
}

void MetricsReporterOmaha::ReportUpdateAttemptMetrics(
    int attempt_number,
    PayloadType payload_type,
    base::TimeDelta duration,
    base::TimeDelta duration_uptime,
    int64_t payload_size,
    metrics::AttemptResult attempt_result,
    ErrorCode internal_error_code) {
  string metric = metrics::kMetricAttemptNumber;
  metrics_lib_->SendToUMA(metric, attempt_number,
                          0,    // min: 0 attempts
                          49,   // max: 49 attempts
                          50);  // num_buckets

  metric = metrics::kMetricAttemptPayloadType;
  metrics_lib_->SendEnumToUMA(metric, payload_type, kNumPayloadTypes);

  metric = metrics::kMetricAttemptDurationMinutes;
  metrics_lib_->SendToUMA(metric, duration.InMinutes(),
                          0,             // min: 0 min
                          10 * 24 * 60,  // max: 10 days
                          50);           // num_buckets

  metric = metrics::kMetricAttemptDurationUptimeMinutes;
  metrics_lib_->SendToUMA(metric, duration_uptime.InMinutes(),
                          0,             // min: 0 min
                          10 * 24 * 60,  // max: 10 days
                          50);           // num_buckets

  metric = metrics::kMetricAttemptPayloadSizeMiB;
  int64_t payload_size_mib = payload_size / kNumBytesInOneMiB;
  metrics_lib_->SendToUMA(metric, payload_size_mib,
                          0,     // min: 0 MiB
                          1024,  // max: 1024 MiB = 1 GiB
                          50);   // num_buckets

  metric = metrics::kMetricAttemptResult;
  metrics_lib_->SendEnumToUMA(
      metric, static_cast<int>(attempt_result),
      static_cast<int>(metrics::AttemptResult::kNumConstants));

  if (internal_error_code != ErrorCode::kSuccess) {
    ReportInternalErrorCode(internal_error_code);
  }

  base::TimeDelta time_since_last;
  if (WallclockDurationHelper(kPrefsMetricsAttemptLastReportingTime,
                              &time_since_last)) {
    metric = metrics::kMetricAttemptTimeSinceLastAttemptMinutes;
    metrics_lib_->SendToUMA(metric, time_since_last.InMinutes(),
                            0,             // min: 0 min
                            30 * 24 * 60,  // max: 30 days
                            50);           // num_buckets
  }

  static int64_t uptime_since_last_storage = 0;
  base::TimeDelta uptime_since_last;
  if (MonotonicDurationHelper(&uptime_since_last_storage, &uptime_since_last)) {
    metric = metrics::kMetricAttemptTimeSinceLastAttemptUptimeMinutes;
    metrics_lib_->SendToUMA(metric, uptime_since_last.InMinutes(),
                            0,             // min: 0 min
                            30 * 24 * 60,  // max: 30 days
                            50);           // num_buckets
  }
}

void MetricsReporterOmaha::ReportUpdateAttemptDownloadMetrics(
    int64_t payload_bytes_downloaded,
    int64_t payload_download_speed_bps,
    DownloadSource download_source,
    metrics::DownloadErrorCode payload_download_error_code,
    metrics::ConnectionType connection_type) {
  string metric = metrics::kMetricAttemptPayloadBytesDownloadedMiB;
  int64_t payload_bytes_downloaded_mib =
      payload_bytes_downloaded / kNumBytesInOneMiB;
  metrics_lib_->SendToUMA(metric, payload_bytes_downloaded_mib,
                          0,     // min: 0 MiB
                          1024,  // max: 1024 MiB = 1 GiB
                          50);   // num_buckets

  metric = metrics::kMetricAttemptPayloadDownloadSpeedKBps;
  int64_t payload_download_speed_kbps = payload_download_speed_bps / 1000;
  metrics_lib_->SendToUMA(metric, payload_download_speed_kbps,
                          0,          // min: 0 kB/s
                          10 * 1000,  // max: 10000 kB/s = 10 MB/s
                          50);        // num_buckets

  metric = metrics::kMetricAttemptDownloadSource;
  metrics_lib_->SendEnumToUMA(metric, download_source, kNumDownloadSources);

  if (payload_download_error_code != metrics::DownloadErrorCode::kUnset) {
    metric = metrics::kMetricAttemptDownloadErrorCode;
    metrics_lib_->SendSparseToUMA(
        metric, static_cast<int>(payload_download_error_code));
  }

  metric = metrics::kMetricAttemptConnectionType;
  metrics_lib_->SendEnumToUMA(
      metric, static_cast<int>(connection_type),
      static_cast<int>(metrics::ConnectionType::kNumConstants));
}

void MetricsReporterOmaha::ReportSuccessfulUpdateMetrics(
    int attempt_count,
    int updates_abandoned_count,
    PayloadType payload_type,
    int64_t payload_size,
    int64_t num_bytes_downloaded[kNumDownloadSources],
    int download_overhead_percentage,
    base::TimeDelta total_duration,
    base::TimeDelta total_duration_uptime,
    int reboot_count,
    int url_switch_count) {
  string metric = metrics::kMetricSuccessfulUpdatePayloadSizeMiB;
  int64_t mbs = payload_size / kNumBytesInOneMiB;
  metrics_lib_->SendToUMA(metric, mbs,
                          0,     // min: 0 MiB
                          1024,  // max: 1024 MiB = 1 GiB
                          50);   // num_buckets

  int64_t total_bytes = 0;
  int download_sources_used = 0;
  for (int i = 0; i < kNumDownloadSources + 1; i++) {
    DownloadSource source = static_cast<DownloadSource>(i);

    // Only consider this download source (and send byte counts) as
    // having been used if we downloaded a non-trivial amount of bytes
    // (e.g. at least 1 MiB) that contributed to the
    // update. Otherwise we're going to end up with a lot of zero-byte
    // events in the histogram.

    metric = metrics::kMetricSuccessfulUpdateBytesDownloadedMiB;
    if (i < kNumDownloadSources) {
      metric += utils::ToString(source);
      mbs = num_bytes_downloaded[i] / kNumBytesInOneMiB;
      total_bytes += num_bytes_downloaded[i];
      if (mbs > 0) {
        download_sources_used |= (1 << i);
      }
    } else {
      mbs = total_bytes / kNumBytesInOneMiB;
    }

    if (mbs > 0) {
      metrics_lib_->SendToUMA(metric, mbs,
                              0,     // min: 0 MiB
                              1024,  // max: 1024 MiB = 1 GiB
                              50);   // num_buckets
    }
  }

  metric = metrics::kMetricSuccessfulUpdateDownloadSourcesUsed;
  metrics_lib_->SendToUMA(metric, download_sources_used,
                          0,                               // min
                          (1 << kNumDownloadSources) - 1,  // max
                          1 << kNumDownloadSources);       // num_buckets

  metric = metrics::kMetricSuccessfulUpdateDownloadOverheadPercentage;
  metrics_lib_->SendToUMA(metric, download_overhead_percentage,
                          0,     // min: 0% overhead
                          1000,  // max: 1000% overhead
                          50);   // num_buckets

  metric = metrics::kMetricSuccessfulUpdateUrlSwitchCount;
  metrics_lib_->SendToUMA(metric, url_switch_count,
                          0,    // min: 0 URL switches
                          49,   // max: 49 URL switches
                          50);  // num_buckets

  metric = metrics::kMetricSuccessfulUpdateTotalDurationMinutes;
  metrics_lib_->SendToUMA(metric, static_cast<int>(total_duration.InMinutes()),
                          0,              // min: 0 min
                          365 * 24 * 60,  // max: 365 days ~= 1 year
                          50);            // num_buckets

  metric = metrics::kMetricSuccessfulUpdateTotalDurationUptimeMinutes;
  metrics_lib_->SendToUMA(metric,
                          static_cast<int>(total_duration_uptime.InMinutes()),
                          0,             // min: 0 min
                          30 * 24 * 60,  // max: 30 days
                          50);           // num_buckets

  metric = metrics::kMetricSuccessfulUpdateRebootCount;
  metrics_lib_->SendToUMA(metric, reboot_count,
                          0,    // min: 0 reboots
                          49,   // max: 49 reboots
                          50);  // num_buckets

  metric = metrics::kMetricSuccessfulUpdatePayloadType;
  metrics_lib_->SendEnumToUMA(metric, payload_type, kNumPayloadTypes);

  metric = metrics::kMetricSuccessfulUpdateAttemptCount;
  metrics_lib_->SendToUMA(metric, attempt_count,
                          1,    // min: 1 attempt
                          50,   // max: 50 attempts
                          50);  // num_buckets

  metric = metrics::kMetricSuccessfulUpdateUpdatesAbandonedCount;
  metrics_lib_->SendToUMA(metric, updates_abandoned_count,
                          0,    // min: 0 counts
                          49,   // max: 49 counts
                          50);  // num_buckets
}

void MetricsReporterOmaha::ReportRollbackMetrics(
    metrics::RollbackResult result) {
  string metric = metrics::kMetricRollbackResult;
  int value = static_cast<int>(result);
  metrics_lib_->SendEnumToUMA(
      metric, value, static_cast<int>(metrics::RollbackResult::kNumConstants));
}

void MetricsReporterOmaha::ReportEnterpriseRollbackMetrics(
    const std::string& metric, const string& rollback_version) {
  int value = utils::VersionPrefix(rollback_version);
  metrics_lib_->SendSparseToUMA(metric, value);
}

void MetricsReporterOmaha::ReportCertificateCheckMetrics(
    ServerToCheck server_to_check, CertificateCheckResult result) {
  string metric;
  switch (server_to_check) {
    case ServerToCheck::kUpdate:
      metric = metrics::kMetricCertificateCheckUpdateCheck;
      break;
    case ServerToCheck::kDownload:
      metric = metrics::kMetricCertificateCheckDownload;
      break;
    case ServerToCheck::kNone:
      return;
  }
  metrics_lib_->SendEnumToUMA(
      metric, static_cast<int>(result),
      static_cast<int>(CertificateCheckResult::kNumConstants));
}

void MetricsReporterOmaha::ReportFailedUpdateCount(int target_attempt) {
  string metric = metrics::kMetricFailedUpdateCount;
  metrics_lib_->SendToUMA(metric, target_attempt,
                          1,   // min value
                          50,  // max value
                          kNumDefaultUmaBuckets);
}

void MetricsReporterOmaha::ReportInvalidatedUpdate(bool successful) {
  string metric = metrics::kMetricInvalidatedUpdate;
  metrics_lib_->SendBoolToUMA(metric, successful);
}

void MetricsReporterOmaha::ReportEnterpriseUpdateInvalidatedResult(
    bool success) {
  string metric = metrics::kMetricEnterpriseUpdateInvalidatedResult;
  metrics_lib_->SendBoolToUMA(metric, success);
}

void MetricsReporterOmaha::ReportInstallDateProvisioningSource(int source,
                                                               int max) {
  metrics_lib_->SendEnumToUMA(metrics::kMetricInstallDateProvisioningSource,
                              source,  // Sample.
                              max);
}

void MetricsReporterOmaha::ReportInternalErrorCode(ErrorCode error_code) {
  auto metric = metrics::kMetricAttemptInternalErrorCode;
  metrics_lib_->SendEnumToUMA(metric, static_cast<int>(error_code),
                              static_cast<int>(ErrorCode::kUmaReportedMax));
}

void MetricsReporterOmaha::ReportConsecutiveUpdateCount(int count) {
  string metric = metrics::kMetricConsecutiveUpdateCount;
  metrics_lib_->SendSparseToUMA(metric, count);
}

void MetricsReporterOmaha::ReportFailedConsecutiveUpdate() {
  string metric = metrics::kMetricConsecutiveUpdateFailed;
  metrics_lib_->SendBoolToUMA(metric, true);
}

bool MetricsReporterOmaha::WallclockDurationHelper(
    const std::string& state_variable_key, TimeDelta* out_duration) {
  bool ret = false;
  Time now = SystemState::Get()->clock()->GetWallclockTime();
  int64_t stored_value;
  if (SystemState::Get()->prefs()->GetInt64(state_variable_key,
                                            &stored_value)) {
    Time stored_time = Time::FromInternalValue(stored_value);
    if (stored_time > now) {
      LOG(ERROR) << "Stored time-stamp used for " << state_variable_key
                 << " is in the future.";
    } else {
      *out_duration = now - stored_time;
      ret = true;
    }
  }

  if (!SystemState::Get()->prefs()->SetInt64(state_variable_key,
                                             now.ToInternalValue())) {
    LOG(ERROR) << "Error storing time-stamp in " << state_variable_key;
  }

  return ret;
}

bool MetricsReporterOmaha::MonotonicDurationHelper(int64_t* storage,
                                                   TimeDelta* out_duration) {
  bool ret = false;
  Time now = SystemState::Get()->clock()->GetMonotonicTime();
  if (*storage != 0) {
    Time stored_time = Time::FromInternalValue(*storage);
    *out_duration = now - stored_time;
    ret = true;
  }
  *storage = now.ToInternalValue();

  return ret;
}

}  // namespace chromeos_update_engine
