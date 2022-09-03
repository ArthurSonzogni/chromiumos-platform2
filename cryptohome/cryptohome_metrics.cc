// Copyright 2014 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/cryptohome_metrics.h"

#include <iterator>
#include <string>

#include <base/logging.h>
#include <base/strings/strcat.h>
#include <base/strings/stringprintf.h>
#include <metrics/metrics_library.h>
#include <metrics/timer.h>

namespace {

struct TimerHistogramParams {
  const char* metric_name;
  int min_sample;
  int max_sample;
  int num_buckets;
};

constexpr char kWrappingKeyDerivationCreateHistogram[] =
    "Cryptohome.WrappingKeyDerivation.Create";
constexpr char kWrappingKeyDerivationMountHistogram[] =
    "Cryptohome.WrappingKeyDerivation.Mount";
constexpr char kCryptohomeErrorHistogram[] = "Cryptohome.Errors";
constexpr char kChecksumStatusHistogram[] = "Cryptohome.ChecksumStatus";
constexpr char kCredentialRevocationResultHistogram[] =
    "Cryptohome.%s.CredentialRevocationResult";
constexpr char kCryptohomeDeletedUserProfilesHistogram[] =
    "Cryptohome.DeletedUserProfiles";
constexpr char kCryptohomeGCacheFreedDiskSpaceInMbHistogram[] =
    "Cryptohome.GCache.FreedDiskSpaceInMb";
constexpr char kCryptohomeCacheVaultFreedDiskSpaceInMbHistogram[] =
    "Cryptohome.FreedCacheVaultDiskSpaceInMb";
constexpr char kCryptohomeFreeDiskSpaceTotalTimeHistogram[] =
    "Cryptohome.FreeDiskSpaceTotalTime2";
constexpr char kCryptohomeLoginDiskCleanupTotalTime[] =
    "Cryptohome.LoginDiskCleanupTotalTime";
constexpr char kCryptohomeFreeDiskSpaceTotalFreedInMbHistogram[] =
    "Cryptohome.FreeDiskSpaceTotalFreedInMb";
constexpr char kCryptohomeFreeDiskSpaceDuringLoginTotalFreedInMbHistogram[] =
    "Cryptohome.FreeDiskSpaceDuringLoginTotalFreedInMb";
constexpr char kCryptohomeTimeBetweenFreeDiskSpaceHistogram[] =
    "Cryptohome.TimeBetweenFreeDiskSpace";
constexpr char kCryptohomeDircryptoMigrationStartStatusHistogram[] =
    "Cryptohome.DircryptoMigrationStartStatus";
constexpr char kCryptohomeDircryptoMigrationEndStatusHistogram[] =
    "Cryptohome.DircryptoMigrationEndStatus";
constexpr char kCryptohomeDircryptoMinimalMigrationStartStatusHistogram[] =
    "Cryptohome.DircryptoMinimalMigrationStartStatus";
constexpr char kCryptohomeDircryptoMinimalMigrationEndStatusHistogram[] =
    "Cryptohome.DircryptoMinimalMigrationEndStatus";
constexpr char kCryptohomeDircryptoMigrationFailedErrorCodeHistogram[] =
    "Cryptohome.DircryptoMigrationFailedErrorCode";
constexpr char kCryptohomeDircryptoMigrationFailedOperationTypeHistogram[] =
    "Cryptohome.DircryptoMigrationFailedOperationType";
constexpr char kCryptohomeDircryptoMigrationFailedPathTypeHistogram[] =
    "Cryptohome.DircryptoMigrationFailedPathType";
constexpr char kCryptohomeDircryptoMigrationTotalByteCountInMbHistogram[] =
    "Cryptohome.DircryptoMigrationTotalByteCountInMb";
constexpr char kCryptohomeDircryptoMigrationTotalFileCountHistogram[] =
    "Cryptohome.DircryptoMigrationTotalFileCount";
constexpr char kCryptohomeDiskCleanupProgressHistogram[] =
    "Cryptohome.DiskCleanupProgress";
constexpr char kCryptohomeDiskCleanupResultHistogram[] =
    "Cryptohome.DiskCleanupResult";
constexpr char kCryptohomeLoginDiskCleanupProgressHistogram[] =
    "Cryptohome.LoginDiskCleanupProgress";
constexpr char kCryptohomeLoginDiskCleanupResultHistogram[] =
    "Cryptohome.LoginDiskCleanupResult";
constexpr char kCryptohomeLEResultHistogramPrefix[] = "Cryptohome.LECredential";
constexpr char kCryptohomeLESyncOutcomeHistogramSuffix[] = ".SyncOutcome";
constexpr char kCryptohomeLELogReplyEntryCountHistogram[] =
    "Cryptohome.LECredential.LogReplayEntryCount";
constexpr char kCryptohomeAsyncDBusRequestsPrefix[] =
    "Cryptohome.AsyncDBusRequest.";
constexpr char kCryptohomeAsyncDBusRequestsInqueueTimePrefix[] =
    "Cryptohome.AsyncDBusRequest.Inqueue.";
constexpr char kCryptohomeParallelTasksPrefix[] = "Cryptohome.ParallelTasks";
constexpr char kHomedirEncryptionTypeHistogram[] =
    "Cryptohome.HomedirEncryptionType";
constexpr char kDircryptoMigrationNoSpaceFailureFreeSpaceInMbHistogram[] =
    "Cryptohome.DircryptoMigrationNoSpaceFailureFreeSpaceInMb";
constexpr char kDircryptoMigrationInitialFreeSpaceInMbHistogram[] =
    "Cryptohome.DircryptoMigrationInitialFreeSpaceInMb";
constexpr char kDircryptoMigrationNoSpaceXattrSizeInBytesHistogram[] =
    "Cryptohome.DircryptoMigrationNoSpaceXattrSizeInBytes";
constexpr char kOOPMountOperationResultHistogram[] =
    "Cryptohome.OOPMountOperationResult";
constexpr char kOOPMountCleanupResultHistogram[] =
    "Cryptohome.OOPMountCleanupResult";
constexpr char kInvalidateDirCryptoKeyResultHistogram[] =
    "Cryptohome.InvalidateDirCryptoKeyResult";
constexpr char kRecoveryPrepareForRemovalResultHistogram[] =
    "Cryptohome.%s.PrepareForRemovalResult";
constexpr char kRestoreSELinuxContextResultForHome[] =
    "Cryptohome.RestoreSELinuxContextResultForHome";
constexpr char kRestoreSELinuxContextResultForShadow[] =
    "Cryptohome.RestoreSELinuxContextResultForShadow";
constexpr char kCreateAuthBlockTypeHistogram[] =
    "Cryptohome.CreateAuthBlockType";
constexpr char kDeriveAuthBlockTypeHistogram[] =
    "Cryptohome.DeriveAuthBlockType";
constexpr char kUserSubdirHasCorrectGroup[] =
    "Cryptohome.UserSubdirHasCorrectGroup";
constexpr char kLegacyCodePathUsageHistogramPrefix[] =
    "Cryptohome.LegacyCodePathUsage";
constexpr char kVaultKeysetMetric[] = "Cryptohome.VaultKeysetMetric";
constexpr char kFetchUssExperimentConfigStatus[] =
    "Cryptohome.UssExperiment.FetchUssExperimentConfigStatus";
constexpr char kFetchUssExperimentConfigRetries[] =
    "Cryptohome.UssExperiment.FetchUssExperimentConfigRetries";
constexpr char kUssExperimentFlag[] =
    "Cryptohome.UssExperiment.UssExperimentFlag";
constexpr char kMaskedDownloadsItems[] = "Cryptohome.MaskedDownloadsItems";

// Histogram parameters. This should match the order of 'TimerType'.
// Min and max samples are in milliseconds.
const TimerHistogramParams kTimerHistogramParams[] = {
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
    // Ext4 crypto migration is expected to takes few minutes in a fast case,
    // and with many tens of thousands of files it may take hours.
    {"Cryptohome.TimeToCompleteDircryptoMigration", 1000, 10 * 60 * 60 * 1000,
     50},
    // Minimal migration is expected to take few seconds in a fast case,
    // and minutes in the worst case if we forgot to blocklist files.
    {"Cryptohome.TimeToCompleteDircryptoMinimalMigration", 200, 2 * 60 * 1000,
     50},

    // OBSOLETE.
    // The out-of-process mount operation will time out after 3 seconds.
    {"Cryptohome.TimeToPerformOOPMountOperation", 0, 3000, 50},

    // OBSOLETE.
    // The out-of-process cleanup operation includes a call to waitpid(2) with
    // a 1-second timeout, so make the max sample a bit higher than that.
    {"Cryptohome.TimeToPerformOOPMountCleanup", 0, 1100, 50},

    // Latency of the LegacyUserSession::Verify operation that gets invoked on
    // session unlock.
    {"Cryptohome.TimeSessionUnlock", 0, 4000, 50},
    {"Cryptohome.TimeToMountGuestEx", 0, 4000, 50},
    // This is only being reported from the out-of-process helper so it's
    // covered by the same 3-second timeout.
    {"Cryptohome.TimeToPerformEphemeralMount", 0, 3000, 50},
    // Non-ephemeral mounts are currently mounted in-process but it makes sense
    // to keep the same scale for them as ephemeral mounts.
    {"Cryptohome.TimeToPerformMount", 0, 3000, 50},
    // The time to generate the ECC auth value in TpmEccAuthBlock.
    {"Cryptohome.TimeToGenerateEccAuthValue", 0, 5000, 50},
    // The time for AuthSession to add a credential.
    {"Cryptohome.TimetoAuthSessionAddCredentials", 0, 6000, 60},
    // The time for AuthSession to add an auth factor with VaultKeyset.
    {"Cryptohome.TimeToAuthSessionAddAuthFactorVK", 0, 6000, 60},
    // The time for AuthSession to add an auth factor with USS.
    {"Cryptohome.TimeToAuthSessionAddAuthFactorUSS", 0, 6000, 60},
    // The time for AuthSession to authenticate a credential.
    {"Cryptohome.TimeToAuthSessionAuthenticate", 0, 6000, 60},
    // The time for AuthSession to authenticate an auth factor with VaultKeyset.
    {"Cryptohome.TimeToAuthSessionAuthenticateAuthFactorVK", 0, 6000, 60},
    // The time for AuthSession to authenticate an auth factor with USS.
    {"Cryptohome.TimeToAuthSessionAuthenticateAuthFactorUSS", 0, 6000, 60},
    // The time for AuthSession to update a credential.
    {"Cryptohome.TimeToAuthSessionUpdateCredentials", 0, 6000, 60},
    // TODO(b/236415538, thomascedeno) - Add metric once UpdateAuthFactor is
    // implemented.
    {"Cryptohome.TimeToAuthSessionUpdateAuthFactorVK", 0, 6000, 60},
    {"Cryptohome.TimeToAuthSessionUpdateAuthFactorUSS", 0, 6000, 60},
    // TODO(b/236415640, thomascedeno) - Add metric once RemoveAuthFactor is
    // implemented.
    {"Cryptohome.TimeToAuthSessionRemoveAuthFactorVK", 0, 6000, 60},
    {"Cryptohome.TimeToAuthSessionRemoveAuthFactorUSS", 0, 6000, 60},
    // Time for User Data Auth class to create a persistent user.
    {"Cryptohome.TimeToCreatePersistentUser", 0, 6000, 60},
    // Time for overall AuthSession lifetime, which
    // has a default of 5 minutes but can be optionally extended.
    {"Cryptohome.AuthSessionTotalLifetime", 0, 3 * 5 * 60 * 1000, 60},
    // Time AuthSession is alive after it is authenticated, does not
    // include time AuthSession is initialized but unauthenticated.
    {"Cryptohome.AuthSessionAuthenticatedLifetime", 0, 3 * 5 * 60 * 1000, 60},
    // The time to Persist a User Secret Stash to system storage.
    {"Cryptohome.TimeToUSSPersist", 0, 5000, 50},
    // The time to Load Persist a User Secret Stash from system storage.
    {"Cryptohome.TimeToUSSLoadPersisted", 0, 5000, 50},
};

static_assert(std::size(kTimerHistogramParams) == cryptohome::kNumTimerTypes,
              "kTimerHistogramParams out of sync with enum TimerType");

// List of strings for a patterned histogram for legacy locations.
const char* kLegacyCodePathLocations[] = {".AddKeyResetSeedGeneration"};

static_assert(
    std::size(kLegacyCodePathLocations) ==
        static_cast<int>(cryptohome::LegacyCodePathLocation::kMaxValue) + 1,
    "kLegacyCodePathLocations out of sync with enum LegacyCodePathLocation");

constexpr char kCryptohomeDeprecatedApiHistogramName[] =
    "Cryptohome.DeprecatedApiCalled";

constexpr char kAttestationStatusHistogramPrefix[] = "Hwsec.Attestation.Status";

// Set to true to disable CryptohomeError related reporting, see
// DisableErrorMetricsReporting().
bool g_disable_error_metrics = false;

MetricsLibraryInterface* g_metrics = NULL;
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

// These values are persisted to logs.
// Keep in sync with respective variant enum in
// tools/metrics/histograms/metadata/cryptohome/histograms.xml
char const* GetAuthBlockTypeStringVariant(cryptohome::AuthBlockType type) {
  switch (type) {
    case cryptohome::AuthBlockType::kPinWeaver:
      return "PinWeaver";
    case cryptohome::AuthBlockType::kChallengeCredential:
      return "ChallengeCredential";
    case cryptohome::AuthBlockType::kDoubleWrappedCompat:
      return "DoubleWrappedCompat";
    case cryptohome::AuthBlockType::kTpmBoundToPcr:
      return "TpmBoundToPcr";
    case cryptohome::AuthBlockType::kTpmNotBoundToPcr:
      return "TpmNotBoundToPcr";
    case cryptohome::AuthBlockType::kScrypt:
      return "Scrypt";
    case cryptohome::AuthBlockType::kCryptohomeRecovery:
      return "CryptohomeRecovery";
    case cryptohome::AuthBlockType::kTpmEcc:
      return "TpmEcc";
    case cryptohome::AuthBlockType::kMaxValue:
      NOTREACHED();
      return "";
  }
}

}  // namespace

namespace cryptohome {

void InitializeMetrics() {
  g_metrics = new MetricsLibrary();
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

void OverrideMetricsLibraryForTesting(MetricsLibraryInterface* lib) {
  g_metrics = lib;
}

void ClearMetricsLibraryForTesting() {
  g_metrics = nullptr;
}

void DisableErrorMetricsReporting() {
  g_disable_error_metrics = true;
}

void ReportWrappingKeyDerivationType(DerivationType derivation_type,
                                     CryptohomePhase crypto_phase) {
  if (!g_metrics) {
    return;
  }

  if (crypto_phase == kCreated) {
    g_metrics->SendEnumToUMA(kWrappingKeyDerivationCreateHistogram,
                             derivation_type, kDerivationTypeNumBuckets);
  } else if (crypto_phase == kMounted) {
    g_metrics->SendEnumToUMA(kWrappingKeyDerivationMountHistogram,
                             derivation_type, kDerivationTypeNumBuckets);
  }
}

void ReportCryptohomeError(CryptohomeErrorMetric error) {
  if (!g_metrics) {
    return;
  }
  g_metrics->SendEnumToUMA(kCryptohomeErrorHistogram, error,
                           kCryptohomeErrorNumBuckets);
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
  bool success = (timer && timer->HasStarted() && timer->Stop() &&
                  timer->ReportMilliseconds());
  if (!success) {
    LOG(WARNING) << "Timer " << kTimerHistogramParams[timer_type].metric_name
                 << " failed to report.";
  }
}

void ReportTimerDuration(
    const AuthSessionPerformanceTimer* auth_session_performance_timer) {
  if (!g_metrics) {
    return;
  }
  // Check that timer_type is a valid timer.
  TimerType timer_type = auth_session_performance_timer->type;
  DCHECK_LT(timer_type, kNumTimerTypes);

  // Parameterize metric by AuthBlockType if needed.
  // kMaxValue is an invalid value, showing that
  // timer_type doesn't need to be parameterized.
  AuthBlockType auth_block_type =
      auth_session_performance_timer->auth_block_type;
  std::string metric_name = kTimerHistogramParams[timer_type].metric_name;
  if (auth_block_type != cryptohome::AuthBlockType::kMaxValue) {
    std::string block_type =
        base::StrCat({".", GetAuthBlockTypeStringVariant(auth_block_type)});
    metric_name.append(block_type);
  }

  auto duration =
      base::TimeTicks::Now() - auth_session_performance_timer->start_time;
  g_metrics->SendToUMA(metric_name, duration.InMilliseconds(),
                       kTimerHistogramParams[timer_type].min_sample,
                       kTimerHistogramParams[timer_type].max_sample,
                       kTimerHistogramParams[timer_type].num_buckets);
}

void ReportTimerDuration(const TimerType& timer_type,
                         base::TimeTicks start_time,
                         const std::string& parameter_string) {
  if (!g_metrics) {
    return;
  }
  // Check that timer_type is a valid timer.
  DCHECK_LT(timer_type, kNumTimerTypes);

  std::string metric_name = kTimerHistogramParams[timer_type].metric_name;
  metric_name.append(parameter_string);

  auto duration = base::TimeTicks::Now() - start_time;
  g_metrics->SendToUMA(metric_name, duration.InMilliseconds(),
                       kTimerHistogramParams[timer_type].min_sample,
                       kTimerHistogramParams[timer_type].max_sample,
                       kTimerHistogramParams[timer_type].num_buckets);
}

void ReportChecksum(ChecksumStatus status) {
  if (!g_metrics) {
    return;
  }
  g_metrics->SendEnumToUMA(kChecksumStatusHistogram, status,
                           kChecksumStatusNumBuckets);
}

void ReportCredentialRevocationResult(AuthBlockType auth_block_type,
                                      LECredError result) {
  if (!g_metrics) {
    return;
  }

  g_metrics->SendEnumToUMA(
      base::StringPrintf(kCredentialRevocationResultHistogram,
                         GetAuthBlockTypeStringVariant(auth_block_type)),
      result, LE_CRED_ERROR_MAX);
}

void ReportFreedGCacheDiskSpaceInMb(int mb) {
  if (!g_metrics) {
    return;
  }
  g_metrics->SendToUMA(kCryptohomeGCacheFreedDiskSpaceInMbHistogram, mb,
                       10 /* 10 MiB minimum */, 1024 * 10 /* 10 GiB maximum */,
                       50 /* number of buckets */);
}

void ReportFreedCacheVaultDiskSpaceInMb(int mb) {
  if (!g_metrics) {
    return;
  }
  g_metrics->SendToUMA(kCryptohomeCacheVaultFreedDiskSpaceInMbHistogram, mb,
                       10 /* 10 MiB minimum */, 1024 * 10 /* 10 GiB maximum */,
                       50 /* number of buckets */);
}

void ReportDeletedUserProfiles(int user_profile_count) {
  if (!g_metrics) {
    return;
  }
  g_metrics->SendToUMA(kCryptohomeDeletedUserProfilesHistogram,
                       user_profile_count, 1 /* minimum */, 100 /* maximum */,
                       20 /* number of buckets */);
}

void ReportFreeDiskSpaceTotalTime(int ms) {
  if (!g_metrics) {
    return;
  }
  g_metrics->SendToUMA(kCryptohomeFreeDiskSpaceTotalTimeHistogram, ms, 1,
                       60 * 1000, 50);
}

void ReportFreeDiskSpaceTotalFreedInMb(int mb) {
  if (!g_metrics) {
    return;
  }
  constexpr int kMin = 1, kMax = 1024 * 10, /* 10 GiB maximum */
      kNumBuckets = 50;
  g_metrics->SendToUMA(kCryptohomeFreeDiskSpaceTotalFreedInMbHistogram, mb,
                       kMin, kMax, kNumBuckets);
}

void ReportTimeBetweenFreeDiskSpace(int s) {
  if (!g_metrics) {
    return;
  }

  constexpr int kMin = 1, kMax = 86400, /* seconds in a day */
      kNumBuckets = 50;
  g_metrics->SendToUMA(kCryptohomeTimeBetweenFreeDiskSpaceHistogram, s, kMin,
                       kMax, kNumBuckets);
}

void ReportLoginDiskCleanupTotalTime(int ms) {
  if (!g_metrics) {
    return;
  }
  g_metrics->SendToUMA(kCryptohomeLoginDiskCleanupTotalTime, ms, 1, 60 * 1000,
                       50);
}

void ReportFreeDiskSpaceDuringLoginTotalFreedInMb(int mb) {
  if (!g_metrics) {
    return;
  }
  constexpr int kMin = 1, kMax = 1024 * 10, /* 10 GiB maximum */
      kNumBuckets = 50;
  g_metrics->SendToUMA(
      kCryptohomeFreeDiskSpaceDuringLoginTotalFreedInMbHistogram, mb, kMin,
      kMax, kNumBuckets);
}

void ReportDircryptoMigrationStartStatus(MigrationType migration_type,
                                         DircryptoMigrationStartStatus status) {
  if (!g_metrics) {
    return;
  }
  const char* metric =
      migration_type == MigrationType::FULL
          ? kCryptohomeDircryptoMigrationStartStatusHistogram
          : kCryptohomeDircryptoMinimalMigrationStartStatusHistogram;
  g_metrics->SendEnumToUMA(metric, status, kMigrationStartStatusNumBuckets);
}

void ReportDircryptoMigrationEndStatus(MigrationType migration_type,
                                       DircryptoMigrationEndStatus status) {
  if (!g_metrics) {
    return;
  }
  const char* metric =
      migration_type == MigrationType::FULL
          ? kCryptohomeDircryptoMigrationEndStatusHistogram
          : kCryptohomeDircryptoMinimalMigrationEndStatusHistogram;
  g_metrics->SendEnumToUMA(metric, status, kMigrationEndStatusNumBuckets);
}

void ReportDircryptoMigrationFailedErrorCode(base::File::Error error_code) {
  if (!g_metrics) {
    return;
  }
  g_metrics->SendEnumToUMA(
      kCryptohomeDircryptoMigrationFailedErrorCodeHistogram, -error_code,
      -base::File::FILE_ERROR_MAX);
}

void ReportDircryptoMigrationFailedOperationType(
    DircryptoMigrationFailedOperationType type) {
  if (!g_metrics) {
    return;
  }
  g_metrics->SendEnumToUMA(
      kCryptohomeDircryptoMigrationFailedOperationTypeHistogram, type,
      kMigrationFailedOperationTypeNumBuckets);
}

void ReportDircryptoMigrationFailedPathType(
    DircryptoMigrationFailedPathType type) {
  if (!g_metrics) {
    return;
  }
  g_metrics->SendEnumToUMA(kCryptohomeDircryptoMigrationFailedPathTypeHistogram,
                           type, kMigrationFailedPathTypeNumBuckets);
}

void ReportDircryptoMigrationTotalByteCountInMb(int total_byte_count_mb) {
  if (!g_metrics) {
    return;
  }
  constexpr int kMin = 1, kMax = 1024 * 1024, kNumBuckets = 50;
  g_metrics->SendToUMA(kCryptohomeDircryptoMigrationTotalByteCountInMbHistogram,
                       total_byte_count_mb, kMin, kMax, kNumBuckets);
}

void ReportDircryptoMigrationTotalFileCount(int total_file_count) {
  if (!g_metrics) {
    return;
  }
  constexpr int kMin = 1, kMax = 100000000, kNumBuckets = 50;
  g_metrics->SendToUMA(kCryptohomeDircryptoMigrationTotalFileCountHistogram,
                       total_file_count, kMin, kMax, kNumBuckets);
}

void ReportDiskCleanupProgress(DiskCleanupProgress progress) {
  if (!g_metrics) {
    return;
  }
  g_metrics->SendEnumToUMA(kCryptohomeDiskCleanupProgressHistogram,
                           static_cast<int>(progress),
                           static_cast<int>(DiskCleanupProgress::kNumBuckets));
}

void ReportDiskCleanupResult(DiskCleanupResult result) {
  if (!g_metrics) {
    return;
  }
  g_metrics->SendEnumToUMA(kCryptohomeDiskCleanupResultHistogram,
                           static_cast<int>(result),
                           static_cast<int>(DiskCleanupResult::kNumBuckets));
}

void ReportLoginDiskCleanupProgress(LoginDiskCleanupProgress progress) {
  if (!g_metrics) {
    return;
  }
  g_metrics->SendEnumToUMA(
      kCryptohomeLoginDiskCleanupProgressHistogram, static_cast<int>(progress),
      static_cast<int>(LoginDiskCleanupProgress::kNumBuckets));
}

void ReportLoginDiskCleanupResult(DiskCleanupResult result) {
  if (!g_metrics) {
    return;
  }
  g_metrics->SendEnumToUMA(kCryptohomeLoginDiskCleanupResultHistogram,
                           static_cast<int>(result),
                           static_cast<int>(DiskCleanupResult::kNumBuckets));
}

void ReportHomedirEncryptionType(HomedirEncryptionType type) {
  if (!g_metrics) {
    return;
  }
  g_metrics->SendEnumToUMA(
      kHomedirEncryptionTypeHistogram, static_cast<int>(type),
      static_cast<int>(
          HomedirEncryptionType::kHomedirEncryptionTypeNumBuckets));
}

void ReportLEResult(const char* type, const char* action, LECredError result) {
  if (!g_metrics) {
    return;
  }

  std::string hist_str = std::string(kCryptohomeLEResultHistogramPrefix)
                             .append(type)
                             .append(action);

  g_metrics->SendEnumToUMA(hist_str, result, LE_CRED_ERROR_MAX);
}

void ReportLESyncOutcome(LECredError result) {
  if (!g_metrics) {
    return;
  }

  std::string hist_str = std::string(kCryptohomeLEResultHistogramPrefix)
                             .append(kCryptohomeLESyncOutcomeHistogramSuffix);

  g_metrics->SendEnumToUMA(hist_str, result, LE_CRED_ERROR_MAX);
}

void ReportLELogReplayEntryCount(size_t entry_count) {
  if (!g_metrics) {
    return;
  }

  constexpr int kMin = 1, kMax = 32, kNumBuckets = 33;
  g_metrics->SendToUMA(kCryptohomeLELogReplyEntryCountHistogram,
                       static_cast<int>(entry_count), kMin, kMax, kNumBuckets);
}

void ReportLEReplayResult(bool is_full_replay, LEReplayError result) {
  if (!g_metrics) {
    return;
  }

  const char* replay_type =
      is_full_replay ? kLEReplayTypeNormal : kLEReplayTypeFull;

  std::string hist_str = std::string(kCryptohomeLEResultHistogramPrefix)
                             .append(kLEOpReplay)
                             .append(replay_type);

  g_metrics->SendEnumToUMA(hist_str, static_cast<int>(result),
                           static_cast<int>(LEReplayError::kMaxValue));
}

void ReportDircryptoMigrationFailedNoSpace(int initial_migration_free_space_mb,
                                           int failure_free_space_mb) {
  if (!g_metrics) {
    return;
  }
  constexpr int kMin = 1, kMax = 1024 * 1024, kNumBuckets = 50;
  g_metrics->SendToUMA(kDircryptoMigrationInitialFreeSpaceInMbHistogram,
                       initial_migration_free_space_mb, kMin, kMax,
                       kNumBuckets);
  g_metrics->SendToUMA(kDircryptoMigrationNoSpaceFailureFreeSpaceInMbHistogram,
                       failure_free_space_mb, kMin, kMax, kNumBuckets);
}

void ReportDircryptoMigrationFailedNoSpaceXattrSizeInBytes(
    int total_xattr_size_bytes) {
  if (!g_metrics) {
    return;
  }
  constexpr int kMin = 1, kMax = 1024 * 1024, kNumBuckets = 50;
  g_metrics->SendToUMA(kDircryptoMigrationNoSpaceXattrSizeInBytesHistogram,
                       total_xattr_size_bytes, kMin, kMax, kNumBuckets);
}

void ReportParallelTasks(int amount_of_task) {
  if (!g_metrics) {
    return;
  }

  constexpr int kMin = 1, kMax = 50, kNumBuckets = 50;
  g_metrics->SendToUMA(kCryptohomeParallelTasksPrefix, amount_of_task, kMin,
                       kMax, kNumBuckets);
}

void ReportAsyncDbusRequestTotalTime(std::string task_name,
                                     base::TimeDelta running_time) {
  if (!g_metrics) {
    return;
  }

  // 3 mins as maximum
  constexpr int kMin = 1, kMax = 3 * 60 * 1000, kNumBuckets = 50;
  g_metrics->SendToUMA(kCryptohomeAsyncDBusRequestsPrefix + task_name,
                       running_time.InMilliseconds(), kMin, kMax, kNumBuckets);
}

void ReportAsyncDbusRequestInqueueTime(std::string task_name,
                                       base::TimeDelta running_time) {
  if (!g_metrics) {
    return;
  }

  // 3 mins as maximum, 3 secs of interval
  constexpr int kMin = 1, kMax = 3 * 60 * 1000, kNumBuckets = 3 * 20;
  g_metrics->SendToUMA(
      kCryptohomeAsyncDBusRequestsInqueueTimePrefix + task_name,
      running_time.InMilliseconds(), kMin, kMax, kNumBuckets);
}

void ReportDeprecatedApiCalled(DeprecatedApiEvent event) {
  if (!g_metrics) {
    return;
  }

  constexpr auto max_event = static_cast<int>(DeprecatedApiEvent::kMaxValue);
  g_metrics->SendEnumToUMA(kCryptohomeDeprecatedApiHistogramName,
                           static_cast<int>(event),
                           static_cast<int>(max_event));
}

void ReportOOPMountOperationResult(OOPMountOperationResult result) {
  if (!g_metrics) {
    return;
  }

  constexpr auto max_event =
      static_cast<int>(OOPMountOperationResult::kMaxValue);
  g_metrics->SendEnumToUMA(kOOPMountOperationResultHistogram,
                           static_cast<int>(result),
                           static_cast<int>(max_event));
}

void ReportOOPMountCleanupResult(OOPMountCleanupResult result) {
  if (!g_metrics) {
    return;
  }

  constexpr auto max_event = static_cast<int>(OOPMountCleanupResult::kMaxValue);
  g_metrics->SendEnumToUMA(kOOPMountCleanupResultHistogram,
                           static_cast<int>(result),
                           static_cast<int>(max_event));
}

void ReportAttestationOpsStatus(const std::string& operation,
                                AttestationOpsStatus status) {
  if (!g_metrics) {
    return;
  }

  const std::string histogram =
      std::string(kAttestationStatusHistogramPrefix) + "." + operation;
  g_metrics->SendEnumToUMA(histogram, static_cast<int>(status),
                           static_cast<int>(AttestationOpsStatus::kMaxValue));
}

void ReportPrepareForRemovalResult(AuthBlockType auth_block_type,
                                   CryptoError result) {
  if (!g_metrics) {
    return;
  }

  g_metrics->SendEnumToUMA(
      base::StringPrintf(kRecoveryPrepareForRemovalResultHistogram,
                         GetAuthBlockTypeStringVariant(auth_block_type)),
      static_cast<int>(result), static_cast<int>(CryptoError::CE_MAX_VALUE));
}

void ReportRestoreSELinuxContextResultForHomeDir(bool success) {
  if (!g_metrics) {
    return;
  }

  g_metrics->SendBoolToUMA(kRestoreSELinuxContextResultForHome, success);
}

void ReportRestoreSELinuxContextResultForShadowDir(bool success) {
  if (!g_metrics) {
    return;
  }

  g_metrics->SendBoolToUMA(kRestoreSELinuxContextResultForShadow, success);
}

void ReportInvalidateDirCryptoKeyResult(bool result) {
  if (!g_metrics) {
    return;
  }

  g_metrics->SendBoolToUMA(kInvalidateDirCryptoKeyResultHistogram, result);
}

void ReportCreateAuthBlock(AuthBlockType type) {
  if (!g_metrics) {
    return;
  }
  g_metrics->SendEnumToUMA(kCreateAuthBlockTypeHistogram,
                           static_cast<int>(type),
                           static_cast<int>(AuthBlockType::kMaxValue));
}

void ReportDeriveAuthBlock(AuthBlockType type) {
  if (!g_metrics) {
    return;
  }
  g_metrics->SendEnumToUMA(kDeriveAuthBlockTypeHistogram,
                           static_cast<int>(type),
                           static_cast<int>(AuthBlockType::kMaxValue));
}

void ReportUserSubdirHasCorrectGroup(bool correct) {
  if (!g_metrics) {
    return;
  }

  g_metrics->SendBoolToUMA(kUserSubdirHasCorrectGroup, correct);
}

void ReportUsageOfLegacyCodePath(const LegacyCodePathLocation location,
                                 bool result) {
  if (!g_metrics) {
    return;
  }

  std::string hist_str =
      std::string(kLegacyCodePathUsageHistogramPrefix)
          .append(kLegacyCodePathLocations[static_cast<int>(location)]);

  g_metrics->SendBoolToUMA(hist_str, result);
}

void ReportVaultKeysetMetrics(const VaultKeysetMetrics& keyset_metrics) {
  if (!g_metrics) {
    return;
  }

  constexpr int kMin = 1, kMax = 99, kNumBuckets = 100;
  g_metrics->SendToUMA(
      std::string(kVaultKeysetMetric).append(".MissingKeyDataCount"),
      keyset_metrics.missing_key_data_count, kMin, kMax, kNumBuckets);
  g_metrics->SendToUMA(
      std::string(kVaultKeysetMetric).append(".EmptyLabelCount"),
      keyset_metrics.empty_label_count, kMin, kMax, kNumBuckets);
  g_metrics->SendToUMA(
      std::string(kVaultKeysetMetric).append(".EmptyLabelPINCount"),
      keyset_metrics.empty_label_le_cred_count, kMin, kMax, kNumBuckets);
  g_metrics->SendToUMA(std::string(kVaultKeysetMetric).append(".PINCount"),
                       keyset_metrics.le_cred_count, kMin, kMax, kNumBuckets);
  g_metrics->SendToUMA(
      std::string(kVaultKeysetMetric).append(".UntypedKeysetCount"),
      keyset_metrics.untyped_count, kMin, kMax, kNumBuckets);
  g_metrics->SendToUMA(
      std::string(kVaultKeysetMetric).append(".SmartUnlockCount"),
      keyset_metrics.smart_unlock_count, kMin, kMax, kNumBuckets);
  g_metrics->SendToUMA(std::string(kVaultKeysetMetric).append(".PasswordCount"),
                       keyset_metrics.password_count, kMin, kMax, kNumBuckets);
  g_metrics->SendToUMA(
      std::string(kVaultKeysetMetric).append(".SmartCardCount"),
      keyset_metrics.smartcard_count, kMin, kMax, kNumBuckets);
  g_metrics->SendToUMA(
      std::string(kVaultKeysetMetric).append(".FingerprintCount"),
      keyset_metrics.fingerprint_count, kMin, kMax, kNumBuckets);
  g_metrics->SendToUMA(std::string(kVaultKeysetMetric).append(".KioskCount"),
                       keyset_metrics.kiosk_count, kMin, kMax, kNumBuckets);
  g_metrics->SendToUMA(
      std::string(kVaultKeysetMetric).append(".UnclassifedKeysetCount"),
      keyset_metrics.unclassified_count, kMin, kMax, kNumBuckets);
}

void ReportMaskedDownloadsItems(int num_items) {
  if (!g_metrics) {
    return;
  }

  constexpr int kMin = 1, kMax = 1000, kNumBuckets = 20;
  g_metrics->SendToUMA(kMaskedDownloadsItems, num_items, kMin, kMax,
                       kNumBuckets);
}

void ReportCryptohomeErrorHashedStack(const uint32_t hashed) {
  if (!g_metrics || g_disable_error_metrics) {
    return;
  }

  g_metrics->SendSparseToUMA(std::string(kCryptohomeErrorHashedStack),
                             static_cast<int>(hashed));
}

void ReportCryptohomeErrorLeaf(const uint32_t node) {
  if (!g_metrics || g_disable_error_metrics) {
    return;
  }

  g_metrics->SendSparseToUMA(std::string(kCryptohomeErrorLeafWithoutTPM),
                             static_cast<int>(node));
}

void ReportCryptohomeErrorLeafWithTPM(const uint32_t mixed) {
  if (!g_metrics || g_disable_error_metrics) {
    return;
  }

  g_metrics->SendSparseToUMA(std::string(kCryptohomeErrorLeafWithTPM),
                             static_cast<int>(mixed));
}

void ReportCryptohomeErrorDevCheckUnexpectedState(const uint32_t loc) {
  if (!g_metrics || g_disable_error_metrics) {
    return;
  }

  g_metrics->SendSparseToUMA(
      std::string(kCryptohomeErrorDevCheckUnexpectedState),
      static_cast<int>(loc));
}

void ReportCryptohomeErrorAllLocations(const uint32_t loc) {
  if (!g_metrics || g_disable_error_metrics) {
    return;
  }

  g_metrics->SendSparseToUMA(std::string(kCryptohomeErrorAllLocations),
                             static_cast<int>(loc));
}

void ReportFetchUssExperimentConfigStatus(
    FetchUssExperimentConfigStatus status) {
  if (!g_metrics) {
    return;
  }

  g_metrics->SendEnumToUMA(
      kFetchUssExperimentConfigStatus, static_cast<int>(status),
      static_cast<int>(FetchUssExperimentConfigStatus::kMaxValue));
}

void ReportFetchUssExperimentConfigRetries(int retries) {
  constexpr int kMin = 0, kMax = 9, kNumBuckets = 10;
  g_metrics->SendToUMA(kFetchUssExperimentConfigRetries, retries, kMin, kMax,
                       kNumBuckets);
}

void ReportUssExperimentFlag(UssExperimentFlag flag) {
  if (!g_metrics) {
    return;
  }

  g_metrics->SendEnumToUMA(kUssExperimentFlag, static_cast<int>(flag),
                           static_cast<int>(UssExperimentFlag::kMaxValue));
}

}  // namespace cryptohome
