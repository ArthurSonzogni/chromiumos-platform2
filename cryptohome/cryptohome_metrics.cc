// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/cryptohome_metrics.h"

#include <base/logging.h>
#include <metrics/metrics_library.h>
#include <metrics/timer.h>

#include "cryptohome/tpm_metrics.h"

namespace {

struct TimerHistogramParams {
  const char* metric_name;
  int min_sample;
  int max_sample;
  int num_buckets;
};

constexpr char kCryptohomeErrorHistogram[] = "Cryptohome.Errors";
constexpr char kDictionaryAttackResetStatusHistogram[] =
    "Platform.TPM.DictionaryAttackResetStatus";
constexpr char kDictionaryAttackCounterHistogram[] =
    "Platform.TPM.DictionaryAttackCounter";
constexpr int kDictionaryAttackCounterNumBuckets = 100;
constexpr char kChecksumStatusHistogram[] = "Cryptohome.ChecksumStatus";
constexpr char kCryptohomeTpmResultsHistogram[] = "Cryptohome.TpmResults";
constexpr char kCryptohomeFreedGCacheDiskSpaceInMbHistogram[] =
    "Cryptohome.FreedGCacheDiskSpaceInMb";
constexpr char kCryptohomeDircryptoMigrationStartStatusHistogram[] =
    "Cryptohome.DircryptoMigrationStartStatus";
constexpr char kCryptohomeDircryptoMigrationEndStatusHistogram[] =
    "Cryptohome.DircryptoMigrationEndStatus";
constexpr char kHomedirEncryptionTypeHistogram[] =
    "Cryptohome.HomedirEncryptionType";

// Histogram parameters. This should match the order of 'TimerType'.
// Min and max samples are in milliseconds.
const TimerHistogramParams kTimerHistogramParams[cryptohome::kNumTimerTypes] = {
    {"Cryptohome.TimeToMountAsync", 0, 4000, 50},
    {"Cryptohome.TimeToMountSync", 0, 4000, 50},
    {"Cryptohome.TimeToMountGuestAsync", 0, 4000, 50},
    {"Cryptohome.TimeToMountGuestSync", 0, 4000, 50},
    {"Cryptohome.TimeToTakeTpmOwnership", 0, 100000, 50},
    // A note on the PKCS#11 initialization time:
    // Max sample for PKCS#11 initialization time is 100s; we are interested
    // in recording the very first PKCS#11 initialization time, which may be a
    // lengthy one. Subsequent initializations are fast (under 1s) because they
    // just check if PKCS#11 was previously initialized, returning immediately.
    // These will all fall into the first histogram bucket.
    {"Cryptohome.TimeToInitPkcs11", 1000, 100000, 50},
    {"Cryptohome.TimeToMountEx", 0, 4000, 50},
    {"Cryptohome.TimeToCompleteDircryptoMigration", 0, 60 * 60 * 1000, 50}};

MetricsLibrary* g_metrics = NULL;
chromeos_metrics::TimerReporter* g_timers[cryptohome::kNumTimerTypes] = {NULL};

chromeos_metrics::TimerReporter* GetTimer(cryptohome::TimerType timer_type) {
  if (!g_timers[timer_type]) {
    g_timers[timer_type] = new chromeos_metrics::TimerReporter(
        kTimerHistogramParams[timer_type].metric_name,
        kTimerHistogramParams[timer_type].min_sample,
        kTimerHistogramParams[timer_type].max_sample,
        kTimerHistogramParams[timer_type].num_buckets);
  }
  return g_timers[timer_type];
}

}  // namespace

namespace cryptohome {

void InitializeMetrics() {
  g_metrics = new MetricsLibrary();
  g_metrics->Init();
  chromeos_metrics::TimerReporter::set_metrics_lib(g_metrics);
}

void TearDownMetrics() {
  if (g_metrics) {
    chromeos_metrics::TimerReporter::set_metrics_lib(NULL);
    delete g_metrics;
    g_metrics = NULL;
  }
  for (int i = 0; i < kNumTimerTypes; ++i) {
    if (g_timers[i]) {
      delete g_timers[i];
    }
  }
}

void ReportCryptohomeError(CryptohomeError error) {
  if (!g_metrics) {
    return;
  }
  g_metrics->SendEnumToUMA(kCryptohomeErrorHistogram,
                           error,
                           kCryptohomeErrorNumBuckets);
}

void ReportTpmResult(TpmReturnCode result) {
  if (!g_metrics) {
    return;
  }
  g_metrics->SendEnumToUMA(kCryptohomeTpmResultsHistogram,
                           GetTpmResultSample(result),
                           kTpmResultNumberOfBuckets);
}

void ReportCrosEvent(const char* event) {
  if (!g_metrics) {
    return;
  }
  g_metrics->SendCrosEventToUMA(event);
}

void ReportTimerStart(TimerType timer_type) {
  if (!g_metrics) {
    return;
  }
  chromeos_metrics::TimerReporter* timer = GetTimer(timer_type);
  if (!timer) {
    return;
  }
  timer->Start();
}

void ReportTimerStop(TimerType timer_type) {
  if (!g_metrics) {
    return;
  }
  chromeos_metrics::TimerReporter* timer = GetTimer(timer_type);
  bool success = (timer && timer->HasStarted() &&
                  timer->Stop() && timer->ReportMilliseconds());
  if (!success) {
    LOG(WARNING) << "Timer " << kTimerHistogramParams[timer_type].metric_name
                 << " failed to report.";
  }
}

void ReportDictionaryAttackResetStatus(DictionaryAttackResetStatus status) {
  if (!g_metrics) {
    return;
  }
  g_metrics->SendEnumToUMA(kDictionaryAttackResetStatusHistogram,
                           status,
                           kDictionaryAttackResetStatusNumBuckets);
}

void ReportDictionaryAttackCounter(int counter) {
  if (!g_metrics) {
    return;
  }
  g_metrics->SendEnumToUMA(kDictionaryAttackCounterHistogram,
                           counter,
                           kDictionaryAttackCounterNumBuckets);
}

void ReportChecksum(ChecksumStatus status) {
  if (!g_metrics) {
    return;
  }
  g_metrics->SendEnumToUMA(kChecksumStatusHistogram,
                           status,
                           kChecksumStatusNumBuckets);
}

void ReportFreedGCacheDiskSpaceInMb(int mb) {
  if (!g_metrics) {
    return;
  }
  g_metrics->SendToUMA(kCryptohomeFreedGCacheDiskSpaceInMbHistogram, mb,
                       0 /* minimum value of the histogram samples */,
                       1000 /* maximum value of the histogram samples (1GB) */,
                       50 /* number of buckets */);
}

void ReportDircryptoMigrationStartStatus(DircryptoMigrationStartStatus status) {
  if (!g_metrics) {
    return;
  }
  g_metrics->SendEnumToUMA(kCryptohomeDircryptoMigrationStartStatusHistogram,
                           status,
                           kMigrationStartStatusNumBuckets);
}

void ReportDircryptoMigrationEndStatus(DircryptoMigrationEndStatus status) {
  if (!g_metrics) {
    return;
  }
  g_metrics->SendEnumToUMA(kCryptohomeDircryptoMigrationEndStatusHistogram,
                           status,
                           kMigrationEndStatusNumBuckets);
}

void ReportHomedirEncryptionType(HomedirEncryptionType type) {
  if (!g_metrics) {
    return;
  }
  g_metrics->SendEnumToUMA(
      kHomedirEncryptionTypeHistogram,
      static_cast<int>(type),
      static_cast<int>(
          HomedirEncryptionType::kHomedirEncryptionTypeNumBuckets));
}
}  // namespace cryptohome
