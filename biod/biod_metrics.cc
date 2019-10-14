// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "biod/biod_metrics.h"

#include <metrics/metrics_library.h>

#include "biod/biod_storage.h"
#include "biod/update_reason.h"
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
constexpr char kMigrationForPositiveMatchSecretResult[] =
    "Fingerprint.Unlock.MigrationForPositiveMatchSecretResult";
}  // namespace metrics

BiodMetrics::BiodMetrics() : metrics_lib_(std::make_unique<MetricsLibrary>()) {}

bool BiodMetrics::SendEnrolledFingerCount(int finger_count) {
  return metrics_lib_->SendEnumToUMA(metrics::kFpEnrolledFingerCount,
                                     finger_count, 10);
}

bool BiodMetrics::SendFpUnlockEnabled(bool enabled) {
  return metrics_lib_->SendBoolToUMA(metrics::kFpUnlockEnabled, enabled);
}

bool BiodMetrics::SendFpLatencyStats(bool matched,
                                     int capture_ms,
                                     int match_ms,
                                     int overall_ms) {
  bool rc = true;
  rc = metrics_lib_->SendToUMA(matched ? metrics::kFpMatchDurationCapture
                                       : metrics::kFpNoMatchDurationCapture,
                               capture_ms, 0, 200, 20) &&
       rc;
  rc = metrics_lib_->SendToUMA(matched ? metrics::kFpMatchDurationMatcher
                                       : metrics::kFpNoMatchDurationMatcher,
                               match_ms, 100, 800, 50) &&
       rc;
  rc = metrics_lib_->SendToUMA(matched ? metrics::kFpMatchDurationOverall
                                       : metrics::kFpNoMatchDurationOverall,
                               overall_ms, 100, 1000, 50) &&
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
  if (!metrics_lib_->SendEnumToUMA(metrics::kUpdaterStatus, to_utype(status),
                                   to_utype(FwUpdaterStatus::kMaxValue))) {
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

  if (!metrics_lib_->SendEnumToUMA(
          metrics::kUpdaterReason, to_utype(reason),
          to_utype(updater::UpdateReason::kMaxValue))) {
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
  return metrics_lib_->SendEnumToUMA(metrics::kRecordFormatVersionMetric,
                                     version, kRecordFormatVersion);
}

bool BiodMetrics::SendMigrationForPositiveMatchSecretResult(bool success) {
  return metrics_lib_->SendBoolToUMA(
      metrics::kMigrationForPositiveMatchSecretResult, success);
}

void BiodMetrics::SetMetricsLibraryForTesting(
    std::unique_ptr<MetricsLibraryInterface> metrics_lib) {
  metrics_lib_ = std::move(metrics_lib);
}

bool BiodMetrics::SendResetContextMode(const FpMode& mode) {
  return metrics_lib_->SendEnumToUMA(metrics::kResetContextMode, mode.EnumVal(),
                                     mode.MaxEnumVal());
}

bool BiodMetrics::SendSetContextMode(const FpMode& mode) {
  return metrics_lib_->SendEnumToUMA(metrics::kSetContextMode, mode.EnumVal(),
                                     mode.MaxEnumVal());
}

bool BiodMetrics::SendSetContextSuccess(bool success) {
  return metrics_lib_->SendBoolToUMA(metrics::kSetContextSuccess, success);
}

}  // namespace biod
