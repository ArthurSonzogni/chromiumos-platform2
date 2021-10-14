// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_CRYPTOHOME_METRICS_H_
#define CRYPTOHOME_CRYPTOHOME_METRICS_H_

#include <string>

#include <base/files/file.h>
#include <base/time/time.h>
#include <metrics/metrics_library.h>

#include "cryptohome/le_credential_manager.h"
#include "cryptohome/migration_type.h"
#include "cryptohome/tpm.h"
#include "cryptohome/tpm_metrics.h"

namespace cryptohome {

// List of all the possible operation types. Used to construct the correct
// histogram while logging to UMA.
// These values are persisted to logs. Entries should not be renumbered and
// numeric values should never be reused.
enum LECredOperationType {
  LE_CRED_OP_RESET_TREE = 0,
  LE_CRED_OP_INSERT = 1,
  LE_CRED_OP_CHECK = 2,
  LE_CRED_OP_RESET = 3,
  LE_CRED_OP_REMOVE = 4,
  LE_CRED_OP_SYNC = 5,
  LE_CRED_OP_MAX
};

// List of all possible actions taken within an LE Credential operation.
// Used to construct the correct histogram while logging to UMA.
// These values are persisted to logs. Entries should not be renumbered and
// numeric values should never be reused.
enum LECredActionType {
  LE_CRED_ACTION_LOAD_FROM_DISK = 0,
  LE_CRED_ACTION_BACKEND = 1,
  LE_CRED_ACTION_SAVE_TO_DISK = 2,
  LE_CRED_ACTION_BACKEND_GET_LOG = 3,
  LE_CRED_ACTION_BACKEND_REPLAY_LOG = 4,
  LE_CRED_ACTION_MAX
};

// The derivation types used in the implementations of AuthBlock class.
// Refer to cryptohome/docs/ for more details.
// These values are persisted to logs. Entries should not be renumbered and
// numeric values should never be reused.
enum DerivationType : int {
  // Derive a high-entropy secret from the user's password using scrypt.
  kScryptBacked = 0,
  // Low-entropy secrets that need brute force protection are mapped to
  // high-entropy secrets that can be obtained via a rate-limited lookup
  // enforced by the TPM/GSC.
  kLowEntropyCredential = 1,
  // Protecting user data via signing cryptographic keys stored on hardware
  // tokens, rather than via passwords. The token needs to present a valid
  // signature for the generated challenge to unseal a secret seed value, which
  // is then used as a KDF passphrase for scrypt to derive the wrapping key.
  // The sealing/unsealing algorithm involves TPM/GSC capabilities for achieving
  // the security strength.
  kSignatureChallengeProtected = 2,
  // TPM/GSC and user passkey is used to derive the wrapping keys which are
  // sealed to PCR.
  kTpmBackedPcrBound = 3,
  // TPM/GSC and user passkey is used to derive the wrapping key.
  kTpmBackedNonPcrBound = 4,
  // Deprecated state - both TPM/GSC and scrypt is being used.
  kDoubleWrapped = 5,
  // Secret is generated on the device and later derived by Cryptohome Recovery
  // process using data stored on the device and by Recovery Mediator service.
  kCryptohomeRecovery = 6,
  // TPM/GSC and user passkey is used to derive the wrapping keys which are
  // sealed to PCR and ECC auth value.
  kTpmBackedEcc = 7,
  kDerivationTypeNumBuckets  // Must be the last entry.
};

// This enum lists the cryptohome phases, used for reporting purposes.
enum CryptohomePhase { kCreated, kMounted };

// These values are persisted to logs. Entries should not be renumbered and
// numeric values should never be reused.
enum CryptohomeError {
  kTpmFail = 1,
  kTcsKeyLoadFailed = 2,
  kTpmDefendLockRunning = 3,
  kDecryptAttemptButTpmKeyMissing = 4,
  kDecryptAttemptButTpmNotOwned = 5,
  kDecryptAttemptButTpmNotAvailable = 6,
  kDecryptAttemptButTpmKeyMismatch = 7,
  kDecryptAttemptWithTpmKeyFailed = 8,
  kCannotLoadTpmSrk = 9,
  kCannotReadTpmSrkPublic = 10,
  kCannotLoadTpmKey = 11,
  kCannotReadTpmPublicKey = 12,
  kTpmBadKeyProperty = 13,
  kLoadPkcs11TokenFailed = 14,
  kEncryptWithTpmFailed = 15,
  kTssCommunicationFailure = 16,
  kTssInvalidHandle = 17,
  kBothTpmAndScryptWrappedKeyset = 18,
  kEphemeralCleanUpFailed = 19,
  kTpmOutOfMemory = 20,
  kCryptohomeErrorNumBuckets  // Must be the last entry.
};

// These values are persisted to logs. Entries should not be renumbered and
// numeric values should never be reused.
enum TimerType {
  kAsyncMountTimer = 0,       // Unused.
  kSyncMountTimer = 1,        // Unused.
  kAsyncGuestMountTimer = 2,  // Unused.
  kSyncGuestMountTimer = 3,   // Unused.
  kTpmTakeOwnershipTimer = 4,
  kPkcs11InitTimer = 5,
  kMountExTimer = 6,
  kDircryptoMigrationTimer = 7,
  kDircryptoMinimalMigrationTimer = 8,
  kOOPMountOperationTimer = 9,
  kOOPMountCleanupTimer = 10,
  kSessionUnlockTimer = 11,
  kMountGuestExTimer = 12,
  kPerformEphemeralMountTimer = 13,
  kPerformMountTimer = 14,
  kGenerateEccAuthValueTimer = 15,
  kNumTimerTypes  // For the number of timer types.
};

// These values are persisted to logs. Entries should not be renumbered and
// numeric values should never be reused.
enum DictionaryAttackResetStatus {
  kResetNotNecessary = 0,
  kResetAttemptSucceeded = 1,
  kResetAttemptFailed = 2,
  kDelegateNotAllowed = 3,
  kDelegateNotAvailable = 4,
  kCounterQueryFailed = 5,
  kInvalidPcr0State = 6,
  kDictionaryAttackResetStatusNumBuckets
};

// These values are persisted to logs. Entries should not be renumbered and
// numeric values should never be reused.
enum ChecksumStatus {
  kChecksumOK = 0,
  kChecksumDoesNotExist = 1,
  kChecksumReadError = 2,
  kChecksumMismatch = 3,
  kChecksumOutOfSync = 4,
  kChecksumStatusNumBuckets
};

// These values are persisted to logs. Entries should not be renumbered and
// numeric values should never be reused.
enum DircryptoMigrationStartStatus {
  kMigrationStarted = 1,
  kMigrationResumed = 2,
  kMigrationStartStatusNumBuckets
};

// These values are persisted to logs. Entries should not be renumbered and
// numeric values should never be reused.
enum DircryptoMigrationEndStatus {
  kNewMigrationFailedGeneric = 1,
  kNewMigrationFinished = 2,
  kResumedMigrationFailedGeneric = 3,
  kResumedMigrationFinished = 4,
  kNewMigrationFailedLowDiskSpace = 5,
  kResumedMigrationFailedLowDiskSpace = 6,
  // The detail of the "FileError" failures (the failed file operation,
  // error code, and the rough classification of the failed path) will be
  // reported in separate metrics, too. Since there's no good way to relate the
  // multi-dimensional metric however, we treat some combinations as special
  // cases and distinguish them here as well.
  kNewMigrationFailedFileError = 7,
  kResumedMigrationFailedFileError = 8,
  kNewMigrationFailedFileErrorOpenEIO = 9,
  kResumedMigrationFailedFileErrorOpenEIO = 10,
  kNewMigrationCancelled = 11,
  kResumedMigrationCancelled = 12,
  kMigrationEndStatusNumBuckets
};

// These values are persisted to logs. Entries should not be renumbered and
// numeric values should never be reused.
enum DircryptoMigrationFailedOperationType {
  kMigrationFailedAtOtherOperation = 1,
  kMigrationFailedAtOpenSourceFile = 2,
  kMigrationFailedAtOpenDestinationFile = 3,
  kMigrationFailedAtCreateLink = 4,
  kMigrationFailedAtDelete = 5,
  kMigrationFailedAtGetAttribute = 6,
  kMigrationFailedAtMkdir = 7,
  kMigrationFailedAtReadLink = 8,
  kMigrationFailedAtSeek = 9,
  kMigrationFailedAtSendfile = 10,
  kMigrationFailedAtSetAttribute = 11,
  kMigrationFailedAtStat = 12,
  kMigrationFailedAtSync = 13,
  kMigrationFailedAtTruncate = 14,
  kMigrationFailedAtOpenSourceFileNonFatal = 15,
  kMigrationFailedAtRemoveAttribute = 16,
  kMigrationFailedOperationTypeNumBuckets
};

// These values are persisted to logs. Entries should not be renumbered and
// numeric values should never be reused.
enum DircryptoMigrationFailedPathType {
  kMigrationFailedUnderOther = 1,
  kMigrationFailedUnderAndroidOther = 2,
  kMigrationFailedUnderAndroidCache = 3,
  kMigrationFailedUnderDownloads = 4,
  kMigrationFailedUnderCache = 5,
  kMigrationFailedUnderGcache = 6,
  kMigrationFailedPathTypeNumBuckets
};

// These values are persisted to logs. Entries should not be renumbered and
// numeric values should never be reused.
enum class HomedirEncryptionType {
  kEcryptfs = 1,
  kDircrypto = 2,
  kDmcrypt = 3,
  kHomedirEncryptionTypeNumBuckets
};

// These values are persisted to logs. Entries should not be renumbered and
// numeric values should never be reused.
enum class DiskCleanupProgress {
  kEphemeralUserProfilesCleaned = 1,
  kBrowserCacheCleanedAboveTarget = 2,
  kGoogleDriveCacheCleanedAboveTarget = 3,
  kGoogleDriveCacheCleanedAboveMinimum = 4,
  kAndroidCacheCleanedAboveTarget = 5,
  kAndroidCacheCleanedAboveMinimum = 6,
  kWholeUserProfilesCleanedAboveTarget = 7,
  kWholeUserProfilesCleaned = 8,
  kNoUnmountedCryptohomes = 9,
  kCacheVaultsCleanedAboveTarget = 10,
  kCacheVaultsCleanedAboveMinimum = 11,
  kNumBuckets
};

// These values are persisted to logs. Entries should not be renumbered and
// numeric values should never be reused.
enum class DiskCleanupResult {
  kDiskCleanupSuccess = 1,
  kDiskCleanupError = 2,
  kDiskCleanupSkip = 3,
  kNumBuckets
};

// Add new deprecated function event here.
// These values are persisted to logs. Entries should not be renumbered and
// numeric values should never be reused.
// Note: All updates here must also update Chrome's enums.xml database.
// Please see this document for more details:
// https://chromium.googlesource.com/chromium/src/+/HEAD/tools/metrics/histograms/
//
// You can view them live here:
// https://uma.googleplex.com/histograms/?histograms=Platform.Cryptohome.DeprecatedApiCalled
enum class DeprecatedApiEvent {
  kInitializeCastKey = 0,
  kGetBootAttribute = 1,
  kSetBootAttribute = 2,
  kFlushAndSignBootAttributes = 3,
  kSignBootLockbox = 4,
  kVerifyBootLockbox = 5,
  kFinalizeBootLockbox = 6,
  kTpmIsBeingOwned = 7,
  kProxyIsMounted = 8,
  kProxyIsMountedForUser = 9,
  kProxyListKeysEx = 10,
  kProxyCheckKeyEx = 11,
  kProxyRemoveKeyEx = 12,
  kProxyMassRemoveKeys = 13,
  kProxyGetKeyDataEx = 14,
  kProxyMigrateKeyEx = 15,
  kProxyAddKeyEx = 16,
  kProxyAddDataRestoreKey = 17,
  kProxyRemoveEx = 18,
  kProxyGetSystemSalt = 19,
  kProxyGetSanitizedUsername = 20,
  kProxyMountEx = 21,
  kProxyMountGuestEx = 22,
  kProxyRenameCryptohome = 23,
  kProxyGetAccountDiskUsage = 24,
  kProxyUnmountEx = 25,
  kProxyUpdateCurrentUserActivityTimestamp = 26,
  kProxyTpmIsReady = 27,
  kProxyTpmIsEnabled = 28,
  kProxyTpmGetPassword = 29,
  kProxyTpmIsOwned = 30,
  kProxyTpmIsBeingOwned = 31,
  kProxyTpmCanAttemptOwnership = 32,
  kProxyTpmClearStoredPassword = 33,
  kProxyTpmIsAttestationPrepared = 34,
  kProxyTpmAttestationGetEnrollmentPreparationsEx = 35,
  kProxyTpmVerifyAttestationData = 36,
  kProxyTpmVerifyEK = 37,
  kProxyTpmAttestationCreateEnrollRequest = 38,
  kProxyAsyncTpmAttestationCreateEnrollRequest = 39,
  kProxyTpmAttestationEnroll = 40,
  kProxyAsyncTpmAttestationEnroll = 41,
  kProxyTpmAttestationEnrollEx = 42,
  kProxyAsyncTpmAttestationEnrollEx = 43,
  kProxyTpmAttestationCreateCertRequest = 44,
  kProxyAsyncTpmAttestationCreateCertRequest = 45,
  kProxyTpmAttestationFinishCertRequest = 46,
  kProxyAsyncTpmAttestationFinishCertRequest = 47,
  kProxyTpmAttestationGetCertificateEx = 48,
  kProxyAsyncTpmAttestationGetCertificateEx = 49,
  kProxyTpmIsAttestationEnrolled = 50,
  kProxyTpmAttestationDoesKeyExist = 51,
  kProxyTpmAttestationGetCertificate = 52,
  kProxyTpmAttestationGetPublicKey = 53,
  kProxyTpmAttestationGetEnrollmentId = 54,
  kProxyTpmAttestationRegisterKey = 55,
  kProxyTpmAttestationSignEnterpriseChallenge = 56,
  kProxyTpmAttestationSignEnterpriseVaChallenge = 57,
  kProxyTpmAttestationSignEnterpriseVaChallengeV2 = 58,
  kProxyTpmAttestationSignSimpleChallenge = 59,
  kProxyTpmAttestationGetKeyPayload = 60,
  kProxyTpmAttestationSetKeyPayload = 61,
  kProxyTpmAttestationDeleteKeys = 62,
  kProxyTpmAttestationDeleteKey = 63,
  kProxyTpmAttestationGetEK = 64,
  kProxyTpmAttestationResetIdentity = 65,
  kProxyTpmGetVersionStructured = 66,
  kProxyPkcs11IsTpmTokenReady = 67,
  kProxyPkcs11GetTpmTokenInfo = 68,
  kProxyPkcs11GetTpmTokenInfoForUser = 69,
  kProxyPkcs11Terminate = 70,
  kProxyGetStatusString = 71,
  kProxyInstallAttributesGet = 72,
  kProxyInstallAttributesSet = 73,
  kProxyInstallAttributesCount = 74,
  kProxyInstallAttributesFinalize = 75,
  kProxyInstallAttributesIsReady = 76,
  kProxyInstallAttributesIsSecure = 77,
  kProxyInstallAttributesIsInvalid = 78,
  kProxyInstallAttributesIsFirstInstall = 79,
  kProxySignBootLockbox = 80,
  kProxyVerifyBootLockbox = 81,
  kProxyFinalizeBootLockbox = 82,
  kProxyGetBootAttribute = 83,
  kProxySetBootAttribute = 84,
  kProxyFlushAndSignBootAttributes = 85,
  kProxyGetLoginStatus = 86,
  kProxyGetTpmStatus = 87,
  kProxyGetEndorsementInfo = 88,
  kProxyInitializeCastKey = 89,
  kProxyStartFingerprintAuthSession = 90,
  kProxyEndFingerprintAuthSession = 91,
  kProxyGetWebAuthnSecret = 92,
  kProxyGetFirmwareManagementParameters = 93,
  kProxySetFirmwareManagementParameters = 94,
  kProxyRemoveFirmwareManagementParameters = 95,
  kProxyMigrateToDircrypto = 96,
  kProxyNeedsDircryptoMigration = 97,
  kProxyGetSupportedKeyPolicies = 98,
  kProxyIsQuotaSupported = 99,
  kProxyGetCurrentSpaceForUid = 100,
  kProxyGetCurrentSpaceForGid = 101,
  kProxyGetCurrentSpaceForProjectId = 102,
  kProxySetProjectId = 103,
  kProxyLockToSingleUserMountUntilReboot = 104,
  kProxyGetRsuDeviceId = 105,
  kProxyCheckHealth = 106,
  kProxyStartAuthSession = 107,
  kProxyAuthenticateAuthSession = 108,
  kProxyAddCredentials = 109,
  kMaxValue
};

// List of the possible results of attempting a mount operation using the
// out-of-process mount helper.
// These values are persisted to logs. Entries should not be renumbered and
// numeric values should never be reused.
enum class OOPMountOperationResult {
  kSuccess = 0,
  kFailedToStart = 1,
  kFailedToWriteRequestProtobuf = 2,
  kHelperProcessTimedOut = 3,
  kFailedToReadResponseProtobuf = 4,
  kMaxValue = kFailedToReadResponseProtobuf
};

// List of the possible results of attempting an unmount/mount clean-up
// using the out-of-process mount helper.
// These values are persisted to logs. Entries should not be renumbered and
// numeric values should never be reused.
enum class OOPMountCleanupResult {
  kSuccess = 0,
  kFailedToPoke = 1,
  kFailedToWait = 2,
  kFailedToKill = 3,
  kMaxValue = kFailedToKill
};

// List of generic results of attestation-related operations. These entries
// should not be renumbered and numeric values should never be reused.
// These values are persisted to logs. Entries should not be renumbered and
// numeric values should never be reused.
enum class AttestationOpsStatus {
  kSuccess = 0,
  kFailure = 1,
  kInvalidPcr0Value = 2,
  kMaxValue
};

// List of all the possible auth block types. Used to construct the correct
// histogram while logging to UMA.
// These values are persisted to logs. Entries should not be renumbered and
// numeric values should never be reused.
enum class AuthBlockType {
  kPinWeaver = 0,
  kChallengeCredential = 1,
  kDoubleWrappedCompat = 2,
  kTpmBoundToPcr = 3,
  kTpmNotBoundToPcr = 4,
  kLibScryptCompat = 5,
  kCryptohomeRecovery = 6,
  kTpmEcc = 7,
  kMaxValue,
};

// Just to make sure I count correctly.
static_assert(static_cast<int>(DeprecatedApiEvent::kMaxValue) == 110,
              "DeprecatedApiEvent Enum miscounted");

// Cros events emitted by cryptohome.
const char kAttestationOriginSpecificIdentifiersExhausted[] =
    "Attestation.OriginSpecificExhausted";
const char kCryptohomeDoubleMount[] = "Cryptohome.DoubleMountRequest";

// Constants related to LE Credential UMA logging.
constexpr char kLEOpResetTree[] = ".ResetTree";
constexpr char kLEOpInsert[] = ".Insert";
constexpr char kLEOpCheck[] = ".Check";
constexpr char kLEOpReset[] = ".Reset";
constexpr char kLEOpRemove[] = ".Remove";
constexpr char kLEOpSync[] = ".Sync";
constexpr char kLEActionLoadFromDisk[] = ".LoadFromDisk";
constexpr char kLEActionBackend[] = ".Backend";
constexpr char kLEActionSaveToDisk[] = ".SaveToDisk";
constexpr char kLEActionBackendGetLog[] = ".BackendGetLog";
constexpr char kLEActionBackendReplayLog[] = ".BackendReplayLog";

// Attestation-related operations. Those are suffixes of the histogram
// kAttestationStatusHistogramPrefix defined in the .cc file.
constexpr char kAttestationDecryptDatabase[] = "DecryptDatabase";
constexpr char kAttestationMigrateDatabase[] = "MigrateDatabase";
constexpr char kAttestationPrepareForEnrollment[] = "PrepareForEnrollment";

// List of all the legacy code paths' usage we are tracking. This will enable us
// to further clean up the code in the future, should any of these code paths
// are found not being used.
// These values are persisted to logs. Entries should not be renumbered and
// numeric values should never be reused.
enum class LegacyCodePathLocation {
  // When a new keyset is being added, Cryptohome checks to see if the keyset
  // that authorizes that add keyset action has a reset_seed.
  // The goal of this block was to support pin, when the older keyset didn't
  // have reset_seed. In the newer versions of keyset, by default, we store a
  // reset_seed.
  kGenerateResetSeedDuringAddKey = 0,
  kMaxValue = kGenerateResetSeedDuringAddKey
};

// Initializes cryptohome metrics. If this is not called, all calls to Report*
// will have no effect.
void InitializeMetrics();

// Cleans up and returns cryptohome metrics to an uninitialized state.
void TearDownMetrics();

// Override the internally used MetricsLibrary for testing purpose.
void OverrideMetricsLibraryForTesting(MetricsLibraryInterface* lib);

// Reset the internally used MetricsLibrary for testing purpose. This is usually
// used with OverrideMetricsLibraryForTesting().
void ClearMetricsLibraryForTesting();

// The |derivation_type| value is reported to the
// "Cryptohome.WrappingKeyDerivation.[Create]/[Mount]" histograms.
// Reported to:
// *.Create - when the cryptohome is being created &
//            when the new wrapping keys are generated for the cryptohome
// *.Mount  - when the cryptohome is being mounted
void ReportWrappingKeyDerivationType(DerivationType derivation_type,
                                     CryptohomePhase crypto_phase);

// The |error| value is reported to the "Cryptohome.Errors" enum histogram.
void ReportCryptohomeError(CryptohomeError error);

// The |result| value is reported to the "Cryptohome.TpmResults" enum histogram.
void ReportTpmResult(TpmResult result);

// Cros events are translated to an enum and reported to the generic
// "Platform.CrOSEvent" enum histogram. The |event| string must be registered in
// metrics/metrics_library.cc:kCrosEventNames.
void ReportCrosEvent(const char* event);

// Starts a timer for the given |timer_type|.
void ReportTimerStart(TimerType timer_type);

// Stops a timer and reports in milliseconds. Timers are reported to the
// "Cryptohome.TimeTo*" histograms.
void ReportTimerStop(TimerType timer_type);

// Reports a status value on the "Platform.TPM.DictionaryAttackResetStatus"
// histogram.
void ReportDictionaryAttackResetStatus(DictionaryAttackResetStatus status);

// Reports a dictionary attack counter value to the
// "Platform.TPM.DictionaryAttackCounter" histogram.
void ReportDictionaryAttackCounter(int counter);

void ReportChecksum(ChecksumStatus status);

// Reports number of deleted user profiles to the
// "Cryptohome.DeletedUserProfiles" histogram.
void ReportDeletedUserProfiles(int user_profile_count);

// Reports total time taken by HomeDirs::FreeDiskSpace cleanup (milliseconds) to
// the "Cryptohome.FreeDiskSpaceTotalTime" histogram.
void ReportFreeDiskSpaceTotalTime(int ms);

// Reports total space freed by HomeDirs::FreeDiskSpace (in MiB) to
// the "Cryptohome.FreeDiskSpaceTotalTime" histogram.
void ReportFreeDiskSpaceTotalFreedInMb(int mb);

// Reports the time between HomeDirs::FreeDiskSpace cleanup calls (seconds) to
// the "Cryptohome.TimeBetweenFreeDiskSpace" histogram.
void ReportTimeBetweenFreeDiskSpace(int s);

// Reports removed GCache size by cryptohome to the
// "Cryptohome.GCache.FreedDiskSpaceInMb" histogram.
void ReportFreedGCacheDiskSpaceInMb(int mb);

// Reports removed Cache Vault size by cryptohome to the
// "Cryptohome.FreedCacheVaultDiskSpaceInMb" histogram.
void ReportFreedCacheVaultDiskSpaceInMb(int mb);

// The |status| value is reported to the
// "Cryptohome.DircryptoMigrationStartStatus" (full migration)
// or the "Cryptohome.DircryptoMinimalMigrationStartStatus" (minimal migration)
// enum histogram.
void ReportDircryptoMigrationStartStatus(MigrationType migration_type,
                                         DircryptoMigrationStartStatus status);

// The |status| value is reported to the
// "Cryptohome.DircryptoMigrationEndStatus" (full migration)
// or the "Cryptohome.DircryptoMinimalMigrationEndStatus" (minimal migration)
// enum histogram.
void ReportDircryptoMigrationEndStatus(MigrationType migration_type,
                                       DircryptoMigrationEndStatus status);

// The |error_code| value is reported to the
// "Cryptohome.DircryptoMigrationFailedErrorCode"
// enum histogram.
void ReportDircryptoMigrationFailedErrorCode(base::File::Error error_code);

// The |type| value is reported to the
// "Cryptohome.DircryptoMigrationFailedOperationType"
// enum histogram.
void ReportDircryptoMigrationFailedOperationType(
    DircryptoMigrationFailedOperationType type);

// The |alerts| data set is reported to the
// "Platform.TPM.HardwareAlerts" enum histogram.
void ReportAlertsData(const Tpm::AlertsData& alerts);

// The |type| value is reported to the
// "Cryptohome.DircryptoMigrationFailedPathType"
// enum histogram.
void ReportDircryptoMigrationFailedPathType(
    DircryptoMigrationFailedPathType type);

// Reports the total byte count in MB to migrate to the
// "Cryptohome.DircryptoMigrationTotalByteCountInMb" histogram.
void ReportDircryptoMigrationTotalByteCountInMb(int total_byte_count_mb);

// Reports the total file count to migrate to the
// "Cryptohome.DircryptoMigrationTotalFileCount" histogram.
void ReportDircryptoMigrationTotalFileCount(int total_file_count);

// Reports which topmost priority was reached to fulfill a cleanup request
// to the "Cryptohome.DiskCleanupProgress" enum histogram.
void ReportDiskCleanupProgress(DiskCleanupProgress progress);

// Report if the automatic disk cleanup encountered an error to the
// "Cryptohome.DiskCleanupResult" enum histogram.
void ReportDiskCleanupResult(DiskCleanupResult result);

// The |type| value is reported to the "Cryptohome.HomedirEncryptionType" enum
// histogram.
void ReportHomedirEncryptionType(HomedirEncryptionType type);

// Reports the result of a Low Entropy (LE) Credential operation to the relevant
// LE Credential histogram.
void ReportLEResult(const char* type, const char* action, LECredError result);

// Reports the overall outcome of a Low Entropy (LE) Credential Sync operation
// to the "Cryptohome.LECredential.SyncOutcome" enum histogram.
void ReportLESyncOutcome(LECredError result);

// Reports the number of log entries attempted to replay during an LE log replay
// operation. This count is one-based, zero is used as a sentinel value for "all
// entries", reported when none of the log entries matches the root hash.
void ReportLELogReplayEntryCount(size_t entry_count);

// Reports the free space in MB when the migration fails and what the free space
// was initially when the migration was started.
void ReportDircryptoMigrationFailedNoSpace(int initial_migration_free_space_mb,
                                           int failure_free_space_mb);

// Reports the total size in bytes of the current xattrs already set on a file
// and the xattr that caused the setxattr call to fail.
void ReportDircryptoMigrationFailedNoSpaceXattrSizeInBytes(
    int total_xattr_size_bytes);

// Reports the total running time of a dbus request.
void ReportAsyncDbusRequestTotalTime(std::string task_name,
                                     base::TimeDelta running_time);

// Reports the total in-queue time of mount thread of a dbus request
void ReportAsyncDbusRequestInqueueTime(std::string task_name,
                                       base::TimeDelta running_time);

// Reports the amount of total tasks waiting in the queue of mount thread.
void ReportParallelTasks(int amount_of_task);

// Reports when a deprecated function that is exposed on the DBus is called.
// This is used to determine which deprecated function is truly dead code,
// and removing it will not trigger side effects.
void ReportDeprecatedApiCalled(DeprecatedApiEvent event);

// Reports the result of an out-of-process mount operation.
void ReportOOPMountOperationResult(OOPMountOperationResult result);

// Reports the result of an out-of-process cleanup operation.
void ReportOOPMountCleanupResult(OOPMountCleanupResult result);

// Reports the result of attestation-related operations. |operation| should be
// one of the suffixes of the histogram kAttestationStatusHistogramPrefix listed
// above.
void ReportAttestationOpsStatus(const std::string& operation,
                                AttestationOpsStatus status);

// Reports the result of an InvalidateDirCryptoKey operation.
void ReportInvalidateDirCryptoKeyResult(bool result);

// Reports the result of a RestoreSELinuxContexts operation for /home/.shadow.
void ReportRestoreSELinuxContextResultForShadowDir(bool success);

// Reports the result of a RestoreSELinuxContexts operation for the bind mounted
// directories under user home directory.
void ReportRestoreSELinuxContextResultForHomeDir(bool success);

// Reports which kinds of auth block we are used to derive.
void ReportCreateAuthBlock(AuthBlockType type);

// Reports which kinds of auth block we are used to derive.
void ReportDeriveAuthBlock(AuthBlockType type);

// Reports whether the existing user subdirectory under the home mount has the
// correct group. This is a temporary metric to diagnose an issue where this
// directory is not owned by group chronos-access.
// TODO(crbug.com/1205308): Remove once the root cause is fixed and we stop
// seeing cases where this directory has the wrong group owner.
void ReportUserSubdirHasCorrectGroup(bool correct);

// Reports which code paths are being used today and performing what actions.
void ReportUsageOfLegacyCodePath(LegacyCodePathLocation location, bool result);

// Initialization helper.
class ScopedMetricsInitializer {
 public:
  ScopedMetricsInitializer() { InitializeMetrics(); }
  ~ScopedMetricsInitializer() { TearDownMetrics(); }
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_CRYPTOHOME_METRICS_H_
