// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "secanomalyd/metrics.h"

#include <base/logging.h>
#include <base/numerics/safe_conversions.h>

#include <metrics/metrics_library.h>

namespace {

constexpr char kSecurityAnomalyHistogramName[] = "ChromeOS.SecurityAnomaly";
constexpr int kSecurityAnomalyHistogramMax = 50;

constexpr char kWXMountCountHistogramName[] = "ChromeOS.WXMountCount";
// The objective of this histogram is to serve as a baseline for W+X mount
// detection. Any non-zero counts of W+X mounts represent a bypass of Verified
// boot and therefore the difference between 5, 10, or 15 W+X mounts is not
// really that important. This could be a boolean histogram as well, but we will
// benefit from knowing what kind of ballpark number of anomalous mounts we're
// talking about, so a regular histogram with a small number of buckets will
// be slightly more beneficial than a boolean one, without consuming that many
// more resources.
constexpr int kWXMountCountHistogramMinBucket = 0;
constexpr int kWXMountCountHistogramMaxBucket = 20;
constexpr int kWXMountCountHistogramNumBuckets = 20;

// The `AnomalousProcCount` prefix is used for histograms that show the count of
// anomalous processes on the system.
// `AttemptedMemfdExec` shows the number of processes on the system that have
// attempted to execute a memory file descriptor.
constexpr char kAttemptedMemfdExecHistogramName[] =
    "ChromeOS.AnomalousProcCount.AttemptedMemfdExec";
// `ForbiddenIntersection` shows the number of processes on the system that are
// not sandboxed to avoid the forbidden intersection:
// https://chromium.googlesource.com/chromiumos/docs/+/HEAD/sandboxing.md#The-forbidden-intersection.
constexpr char kForbiddenIntersectionHistogramName[] =
    "ChromeOS.AnomalousProcCount.ForbiddenIntersection";
constexpr int kAnomalousProcCountMinBucket = 0;
constexpr int kAnomalousProcCountMaxBucket = 100;
constexpr int kAnomalousProcCountNumBuckets = 100;

// The `Sandboxing` prefix is used for metrics regarding the sandboxing state of
// the system.
constexpr char kLandlockEnabledHistogramName[] =
    "ChromeOS.Sandboxing.LandlockEnabled";
constexpr char kSecCompCoverageHistogramName[] =
    "ChromeOS.Sandboxing.SecCompCoverage";
constexpr char kNnpProcPercentageHistogramName[] =
    "ChromeOS.Sandboxing.NoNewPrivsProcPercentage";
constexpr char kNonRootProcPercentageHistogramName[] =
    "ChromeOS.Sandboxing.NonRootProcPercentage";
constexpr char kUnprivProcPercentageHistogramName[] =
    "ChromeOS.Sandboxing.UnprivProcPercentage";
constexpr char kNonInitNsProcPercentageHistogramName[] =
    "ChromeOS.Sandboxing.NonInitNsProcPercentage";

constexpr char kAnomalyUploadSuccess[] =
    "ChromeOS.SecurityAnomalyUploadSuccess";

MetricsLibraryInterface* metrics_library = nullptr;

void InitializeMetricsIfNecessary() {
  if (!metrics_library) {
    metrics_library = new MetricsLibrary();
  }
}

}  // namespace

bool SendSecurityAnomalyToUMA(SecurityAnomaly secanomaly) {
  InitializeMetricsIfNecessary();
  return metrics_library->SendEnumToUMA(kSecurityAnomalyHistogramName,
                                        static_cast<int>(secanomaly),
                                        kSecurityAnomalyHistogramMax);
}

bool SendWXMountCountToUMA(size_t wx_mount_count) {
  InitializeMetricsIfNecessary();
  return metrics_library->SendToUMA(
      kWXMountCountHistogramName, base::checked_cast<int>(wx_mount_count),
      kWXMountCountHistogramMinBucket, kWXMountCountHistogramMaxBucket,
      kWXMountCountHistogramNumBuckets);
}

bool SendForbiddenIntersectionProcCountToUMA(size_t proc_count) {
  InitializeMetricsIfNecessary();
  return metrics_library->SendToUMA(
      kForbiddenIntersectionHistogramName, base::checked_cast<int>(proc_count),
      kAnomalousProcCountMinBucket, kAnomalousProcCountMaxBucket,
      kAnomalousProcCountNumBuckets);
}

bool SendAttemptedMemfdExecProcCountToUMA(size_t proc_count) {
  InitializeMetricsIfNecessary();
  return metrics_library->SendToUMA(
      kAttemptedMemfdExecHistogramName, base::checked_cast<int>(proc_count),
      kAnomalousProcCountMinBucket, kAnomalousProcCountMaxBucket,
      kAnomalousProcCountNumBuckets);
}

bool SendLandlockStatusToUMA(bool enabled) {
  InitializeMetricsIfNecessary();
  return metrics_library->SendBoolToUMA(kLandlockEnabledHistogramName, enabled);
}

bool SendSecCompCoverageToUMA(unsigned int coverage_percentage) {
  InitializeMetricsIfNecessary();
  return metrics_library->SendPercentageToUMA(
      kSecCompCoverageHistogramName, static_cast<int>(coverage_percentage));
}

bool SendNnpProcPercentageToUMA(unsigned int proc_percentage) {
  InitializeMetricsIfNecessary();
  return metrics_library->SendPercentageToUMA(
      kNnpProcPercentageHistogramName, static_cast<int>(proc_percentage));
}

bool SendNonRootProcPercentageToUMA(unsigned int proc_percentage) {
  InitializeMetricsIfNecessary();
  return metrics_library->SendPercentageToUMA(
      kNonRootProcPercentageHistogramName, static_cast<int>(proc_percentage));
}

bool SendUnprivProcPercentageToUMA(unsigned int proc_percentage) {
  InitializeMetricsIfNecessary();
  return metrics_library->SendPercentageToUMA(
      kUnprivProcPercentageHistogramName, static_cast<int>(proc_percentage));
}

bool SendNonInitNsProcPercentageToUMA(unsigned int proc_percentage) {
  InitializeMetricsIfNecessary();
  return metrics_library->SendPercentageToUMA(
      kNonInitNsProcPercentageHistogramName, static_cast<int>(proc_percentage));
}

bool SendAnomalyUploadResultToUMA(bool success) {
  InitializeMetricsIfNecessary();
  return metrics_library->SendBoolToUMA(kAnomalyUploadSuccess, success);
}
