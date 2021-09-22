// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "biod/biod_metrics.h"

#include <base/stl_util.h>
#include <libec/fingerprint/fp_sensor_errors.h>
#include <metrics/metrics_library.h>

#include "biod/biod_storage.h"
#include "biod/updater/update_reason.h"
#include "biod/utils.h"

namespace biod {

namespace metrics {

constexpr char kFpUnlockEnabled[] = "Fingerprint.UnlockEnabled";
constexpr char kFpEnrolledFingerCount[] =
    "Fingerprint.Unlock.EnrolledFingerCount";
constexpr char kFpMatchDurationCapture[] =
    "Fingerprint.Unlock.Match.Duration.Capture";
constexpr char kFpMatchDurationMatcher[] =
    "Fingerprint.Unlock.Match.Duration.Matcher";
constexpr char kFpMatchDurationOverall[] =
    "Fingerprint.Unlock.Match.Duration.Overall";
constexpr char kFpNoMatchDurationCapture[] =
    "Fingerprint.Unlock.NoMatch.Duration.Capture";
constexpr char kFpNoMatchDurationMatcher[] =
    "Fingerprint.Unlock.NoMatch.Duration.Matcher";
constexpr char kFpNoMatchDurationOverall[] =
    "Fingerprint.Unlock.NoMatch.Duration.Overall";
constexpr char kFpMatchIgnoredDueToPowerButtonPress[] =
    "Fingerprint.Unlock.MatchIgnoredDueToPowerButtonPress";
constexpr char kResetContextMode[] = "Fingerprint.Reset.ResetContextMode";
constexpr char kSetContextMode[] = "Fingerprint.SetContext.SetContextMode";
constexpr char kSetContextSuccess[] = "Fingerprint.SetContext.Success";
constexpr char kUpdaterStatus[] = "Fingerprint.Updater.Status";
constexpr char kUpdaterReason[] = "Fingerprint.Updater.Reason";
constexpr char kUpdaterDurationNoUpdate[] =
    "Fingerprint.Updater.NoUpdate.Duration.Overall";
constexpr char kUpdaterDurationUpdate[] =
    "Fingerprint.Updater.Update.Duration.Overall";
constexpr char kFpReadPositiveMatchSecretSuccessOnMatch[] =
    "Fingerprint.Unlock.ReadPositiveMatchSecret.Success";
constexpr char kFpPositiveMatchSecretCorrect[] =
    "Fingerprint.Unlock.Match.PositiveMatchSecretCorrect";
constexpr char kRecordFormatVersionMetric[] =
    "Fingerprint.Unlock.RecordFormatVersion";
constexpr char kNumDeadPixels[] = "Fingerprint.Sensor.NumDeadPixels";
constexpr char kUploadTemplateSuccess[] = "Fingerprint.UploadTemplate.Success";

// See
// https://chromium.googlesource.com/chromium/src.git/+/HEAD/tools/metrics/histograms/README.md#count-histograms_choosing-number-of-buckets
constexpr int kDefaultNumBuckets = 50;

// Upper boundary to use in EC result related histograms. This follows
// "enum ec_status" in ec_commands.h. We do not use EC_RES_MAX because that
// value is too large for the histogram.
constexpr int kMaxEcResultCode = 20;

}  // namespace metrics

BiodMetrics::BiodMetrics() : metrics_lib_(std::make_unique<MetricsLibrary>()) {}

bool BiodMetrics::SendEnrolledFingerCount(int finger_count) {
  return metrics_lib_->SendEnumToUMA(metrics::kFpEnrolledFingerCount,
                                     finger_count, 10);
}

bool BiodMetrics::SendFpUnlockEnabled(bool enabled) {
  return metrics_lib_->SendBoolToUMA(metrics::kFpUnlockEnabled, enabled);
}

bool BiodMetrics::SendFpLatencyStats(
    bool matched, const CrosFpDeviceInterface::FpStats& stats) {
  bool rc = true;
  rc = metrics_lib_->SendToUMA(matched ? metrics::kFpMatchDurationCapture
                                       : metrics::kFpNoMatchDurationCapture,
                               stats.capture_ms, 0, 200, 20) &&
       rc;
  rc = metrics_lib_->SendToUMA(matched ? metrics::kFpMatchDurationMatcher
                                       : metrics::kFpNoMatchDurationMatcher,
                               stats.matcher_ms, 100, 800, 50) &&
       rc;
  rc = metrics_lib_->SendToUMA(matched ? metrics::kFpMatchDurationOverall
                                       : metrics::kFpNoMatchDurationOverall,
                               stats.overall_ms, 100, 1000, 50) &&
       rc;
  return rc;
}

bool BiodMetrics::SendFwUpdaterStatus(FwUpdaterStatus status,
                                      updater::UpdateReason reason,
                                      int overall_ms) {
  // The following presents the updater timing tests results for nocturne,
  // which uses the dartmonkey board with a large 2M firmware image on a
  // Cortex M7:
  // * no update takes about 60ms at boot
  // * 10s boot-splash-screen timeout with update RO+RW takes about 83s.
  // * 10s boot-splash-screen timeout with update RW(~35s) takes about 44s.
  // * 10s boot-splash-screen timeout with update RO(~32s) takes about 39s.
  // Note, we strive to allocate as few bins as possible, so we let the target
  // resolution steer our bucket counts.
  constexpr int kNoUpdateMaxMSec = 500;
  constexpr int kNoUpdateResolutionMSec = 10;
  constexpr int kNoUpdateBuckets = kNoUpdateMaxMSec / kNoUpdateResolutionMSec;
  constexpr int kUpdateMaxMSec = 2 * 60 * 1000;
  constexpr int kUpdateResolutionMSec = 2400;
  constexpr int kUpdateBuckets = kUpdateMaxMSec / kUpdateResolutionMSec;

  bool rc = true;
  // TODO(crbug.com/1218246) Change UMA enum name kUpdaterStatus if new enums
  // for FWUpdaterStatus are added to avoid data discontinuity, then use
  // kMaxValue+1 rather than kMaxValue (or templated SendEnumToUMA()).
  if (!metrics_lib_->SendEnumToUMA(
          metrics::kUpdaterStatus, base::to_underlying(status),
          base::to_underlying(FwUpdaterStatus::kMaxValue))) {
    rc = false;
  }

  if (status == FwUpdaterStatus::kUnnecessary) {
    if (!metrics_lib_->SendToUMA(metrics::kUpdaterDurationNoUpdate, overall_ms,
                                 0, kNoUpdateMaxMSec, kNoUpdateBuckets)) {
      rc = false;
    }
  } else {
    if (!metrics_lib_->SendToUMA(metrics::kUpdaterDurationUpdate, overall_ms, 0,
                                 kUpdateMaxMSec, kUpdateBuckets)) {
      rc = false;
    }
  }

  if (!metrics_lib_->SendEnumToUMA(metrics::kUpdaterReason, reason)) {
    rc = false;
  }

  return rc;
}

bool BiodMetrics::SendIgnoreMatchEventOnPowerButtonPress(bool is_ignored) {
  return metrics_lib_->SendBoolToUMA(
      metrics::kFpMatchIgnoredDueToPowerButtonPress, is_ignored);
}

bool BiodMetrics::SendReadPositiveMatchSecretSuccess(bool success) {
  return metrics_lib_->SendBoolToUMA(
      metrics::kFpReadPositiveMatchSecretSuccessOnMatch, success);
}

bool BiodMetrics::SendPositiveMatchSecretCorrect(bool correct) {
  return metrics_lib_->SendBoolToUMA(metrics::kFpPositiveMatchSecretCorrect,
                                     correct);
}

bool BiodMetrics::SendRecordFormatVersion(int version) {
  // TODO(crbug.com/1218246) Change UMA enum name kRecordFormatVersionMetric if
  // kRecordFormatVersion changes to avoid data discontinuity, then use
  // kRecordFormatVersion+1 rather than kRecordFormatVersion for
  // 'exclusive_max'.
  return metrics_lib_->SendEnumToUMA(metrics::kRecordFormatVersionMetric,
                                     version, kRecordFormatVersion);
}

void BiodMetrics::SetMetricsLibraryForTesting(
    std::unique_ptr<MetricsLibraryInterface> metrics_lib) {
  metrics_lib_ = std::move(metrics_lib);
}

bool BiodMetrics::SendResetContextMode(const ec::FpMode& mode) {
  return metrics_lib_->SendEnumToUMA(metrics::kResetContextMode, mode.EnumVal(),
                                     mode.MaxEnumVal());
}

bool BiodMetrics::SendSetContextMode(const ec::FpMode& mode) {
  return metrics_lib_->SendEnumToUMA(metrics::kSetContextMode, mode.EnumVal(),
                                     mode.MaxEnumVal());
}

bool BiodMetrics::SendSetContextSuccess(bool success) {
  return metrics_lib_->SendBoolToUMA(metrics::kSetContextSuccess, success);
}

bool BiodMetrics::SendDeadPixelCount(int num_dead_pixels) {
  constexpr int min_dead = 0;
  constexpr int max_dead = ec::kMaxDeadPixels;
  return metrics_lib_->SendToUMA(metrics::kNumDeadPixels, num_dead_pixels,
                                 min_dead, max_dead,
                                 metrics::kDefaultNumBuckets);
}

bool BiodMetrics::SendUploadTemplateResult(int ec_result) {
  constexpr int min_ec_result_code = metrics::kCmdRunFailure;
  return metrics_lib_->SendToUMA(
      metrics::kUploadTemplateSuccess, ec_result, min_ec_result_code,
      metrics::kMaxEcResultCode,
      metrics::kMaxEcResultCode - min_ec_result_code + 1);
}

}  // namespace biod
