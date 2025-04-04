// Copyright 2012 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "update_engine/common/error_code_utils.h"

#include <base/logging.h>
#include <base/strings/string_number_conversions.h>

using std::string;

namespace chromeos_update_engine {
namespace utils {

const char kCategoryPayload[] = "payload";
const char kCategoryDownload[] = "download";
const char kCategoryVerity[] = "verity";

// Error detail of alert, note that all phrases here are negative.
const char kErrorMismatch[] = "mismatch";
const char kErrorVerification[] = "verification failed";
const char kErrorVersion[] = "unsupported version";
const char kErrorTimestamp[] = "timestamp error";
const char kErrorSignature[] = "signature error";
const char kErrorManifest[] = "manifest error";

namespace {
ErrorCode StripErrorCode(ErrorCode code) {
  // If the given code has both parts (i.e. the error code part and the flags
  // part) then strip off the flags part since the switch statements below
  // have case statements only for the base error code or a single flag but
  // don't support any combinations of those.
  if ((static_cast<int>(code) & static_cast<int>(ErrorCode::kSpecialFlags)) &&
      (static_cast<int>(code) & ~static_cast<int>(ErrorCode::kSpecialFlags))) {
    code = static_cast<ErrorCode>(static_cast<int>(code) &
                                  ~static_cast<int>(ErrorCode::kSpecialFlags));
  }
  return code;
}
}  // namespace

string ErrorCodeToString(ErrorCode code) {
  code = StripErrorCode(code);
  switch (code) {
    case ErrorCode::kSuccess:
      return "ErrorCode::kSuccess";
    case ErrorCode::kError:
      return "ErrorCode::kError";
    case ErrorCode::kOmahaRequestError:
      return "ErrorCode::kOmahaRequestError";
    case ErrorCode::kOmahaResponseHandlerError:
      return "ErrorCode::kOmahaResponseHandlerError";
    case ErrorCode::kFilesystemCopierError:
      return "ErrorCode::kFilesystemCopierError";
    case ErrorCode::kPostinstallRunnerError:
      return "ErrorCode::kPostinstallRunnerError";
    case ErrorCode::kPayloadMismatchedType:
      return "ErrorCode::kPayloadMismatchedType";
    case ErrorCode::kInstallDeviceOpenError:
      return "ErrorCode::kInstallDeviceOpenError";
    case ErrorCode::kKernelDeviceOpenError:
      return "ErrorCode::kKernelDeviceOpenError";
    case ErrorCode::kDownloadTransferError:
      return "ErrorCode::kDownloadTransferError";
    case ErrorCode::kPayloadHashMismatchError:
      return "ErrorCode::kPayloadHashMismatchError";
    case ErrorCode::kPayloadSizeMismatchError:
      return "ErrorCode::kPayloadSizeMismatchError";
    case ErrorCode::kDownloadPayloadVerificationError:
      return "ErrorCode::kDownloadPayloadVerificationError";
    case ErrorCode::kDownloadNewPartitionInfoError:
      return "ErrorCode::kDownloadNewPartitionInfoError";
    case ErrorCode::kDownloadWriteError:
      return "ErrorCode::kDownloadWriteError";
    case ErrorCode::kNewRootfsVerificationError:
      return "ErrorCode::kNewRootfsVerificationError";
    case ErrorCode::kNewKernelVerificationError:
      return "ErrorCode::kNewKernelVerificationError";
    case ErrorCode::kSignedDeltaPayloadExpectedError:
      return "ErrorCode::kSignedDeltaPayloadExpectedError";
    case ErrorCode::kDownloadPayloadPubKeyVerificationError:
      return "ErrorCode::kDownloadPayloadPubKeyVerificationError";
    case ErrorCode::kPostinstallBootedFromFirmwareB:
      return "ErrorCode::kPostinstallBootedFromFirmwareB";
    case ErrorCode::kDownloadStateInitializationError:
      return "ErrorCode::kDownloadStateInitializationError";
    case ErrorCode::kDownloadInvalidMetadataMagicString:
      return "ErrorCode::kDownloadInvalidMetadataMagicString";
    case ErrorCode::kDownloadSignatureMissingInManifest:
      return "ErrorCode::kDownloadSignatureMissingInManifest";
    case ErrorCode::kDownloadManifestParseError:
      return "ErrorCode::kDownloadManifestParseError";
    case ErrorCode::kDownloadMetadataSignatureError:
      return "ErrorCode::kDownloadMetadataSignatureError";
    case ErrorCode::kDownloadMetadataSignatureVerificationError:
      return "ErrorCode::kDownloadMetadataSignatureVerificationError";
    case ErrorCode::kDownloadMetadataSignatureMismatch:
      return "ErrorCode::kDownloadMetadataSignatureMismatch";
    case ErrorCode::kDownloadOperationHashVerificationError:
      return "ErrorCode::kDownloadOperationHashVerificationError";
    case ErrorCode::kDownloadOperationExecutionError:
      return "ErrorCode::kDownloadOperationExecutionError";
    case ErrorCode::kDownloadOperationHashMismatch:
      return "ErrorCode::kDownloadOperationHashMismatch";
    case ErrorCode::kOmahaRequestEmptyResponseError:
      return "ErrorCode::kOmahaRequestEmptyResponseError";
    case ErrorCode::kOmahaRequestXMLParseError:
      return "ErrorCode::kOmahaRequestXMLParseError";
    case ErrorCode::kDownloadInvalidMetadataSize:
      return "ErrorCode::kDownloadInvalidMetadataSize";
    case ErrorCode::kDownloadInvalidMetadataSignature:
      return "ErrorCode::kDownloadInvalidMetadataSignature";
    case ErrorCode::kOmahaResponseInvalid:
      return "ErrorCode::kOmahaResponseInvalid";
    case ErrorCode::kOmahaUpdateIgnoredPerPolicy:
      return "ErrorCode::kOmahaUpdateIgnoredPerPolicy";
    case ErrorCode::kOmahaUpdateDeferredPerPolicy:
      return "ErrorCode::kOmahaUpdateDeferredPerPolicy";
    case ErrorCode::kOmahaErrorInHTTPResponse:
      return "ErrorCode::kOmahaErrorInHTTPResponse";
    case ErrorCode::kDownloadOperationHashMissingError:
      return "ErrorCode::kDownloadOperationHashMissingError";
    case ErrorCode::kDownloadMetadataSignatureMissingError:
      return "ErrorCode::kDownloadMetadataSignatureMissingError";
    case ErrorCode::kOmahaUpdateDeferredForBackoff:
      return "ErrorCode::kOmahaUpdateDeferredForBackoff";
    case ErrorCode::kPostinstallPowerwashError:
      return "ErrorCode::kPostinstallPowerwashError";
    case ErrorCode::kUpdateCanceledByChannelChange:
      return "ErrorCode::kUpdateCanceledByChannelChange";
    case ErrorCode::kUmaReportedMax:
      return "ErrorCode::kUmaReportedMax";
    case ErrorCode::kOmahaRequestHTTPResponseBase:
      return "ErrorCode::kOmahaRequestHTTPResponseBase";
    case ErrorCode::kResumedFlag:
      return "Resumed";
    case ErrorCode::kDevModeFlag:
      return "DevMode";
    case ErrorCode::kTestImageFlag:
      return "TestImage";
    case ErrorCode::kTestOmahaUrlFlag:
      return "TestOmahaUrl";
    case ErrorCode::kSpecialFlags:
      return "ErrorCode::kSpecialFlags";
    case ErrorCode::kPostinstallFirmwareRONotUpdatable:
      return "ErrorCode::kPostinstallFirmwareRONotUpdatable";
    case ErrorCode::kUnsupportedMajorPayloadVersion:
      return "ErrorCode::kUnsupportedMajorPayloadVersion";
    case ErrorCode::kUnsupportedMinorPayloadVersion:
      return "ErrorCode::kUnsupportedMinorPayloadVersion";
    case ErrorCode::kOmahaRequestXMLHasEntityDecl:
      return "ErrorCode::kOmahaRequestXMLHasEntityDecl";
    case ErrorCode::kFilesystemVerifierError:
      return "ErrorCode::kFilesystemVerifierError";
    case ErrorCode::kUserCanceled:
      return "ErrorCode::kUserCanceled";
    case ErrorCode::kNonCriticalUpdateInOOBE:
      return "ErrorCode::kNonCriticalUpdateInOOBE";
    case ErrorCode::kOmahaUpdateIgnoredOverCellular:
      return "ErrorCode::kOmahaUpdateIgnoredOverCellular";
    case ErrorCode::kPayloadTimestampError:
      return "ErrorCode::kPayloadTimestampError";
    case ErrorCode::kUpdatedButNotActive:
      return "ErrorCode::kUpdatedButNotActive";
    case ErrorCode::kNoUpdate:
      return "ErrorCode::kNoUpdate";
    case ErrorCode::kRollbackNotPossible:
      return "ErrorCode::kRollbackNotPossible";
    case ErrorCode::kFirstActiveOmahaPingSentPersistenceError:
      return "ErrorCode::kFirstActiveOmahaPingSentPersistenceError";
    case ErrorCode::kVerityCalculationError:
      return "ErrorCode::kVerityCalculationError";
    case ErrorCode::kInternalLibCurlError:
      return "ErrorCode::kInternalLibCurlError";
    case ErrorCode::kUnresolvedHostError:
      return "ErrorCode::kUnresolvedHostError";
    case ErrorCode::kUnresolvedHostRecovered:
      return "ErrorCode::kUnresolvedHostRecovered";
    case ErrorCode::kNotEnoughSpace:
      return "ErrorCode::kNotEnoughSpace";
    case ErrorCode::kDeviceCorrupted:
      return "ErrorCode::kDeviceCorrupted";
    case ErrorCode::kPackageExcludedFromUpdate:
      return "ErrorCode::kPackageExcludedFromUpdate";
    case ErrorCode::kDownloadCancelledPerPolicy:
      return "ErrorCode::kDownloadCancelledPerPolicy";
    case ErrorCode::kRepeatedFpFromOmahaError:
      return "ErrorCode::kRepeatedFpFromOmahaError";
    case ErrorCode::kInvalidateLastUpdate:
      return "ErrorCode::kInvalidateLastUpdate";
    case ErrorCode::kOmahaUpdateIgnoredOverMetered:
      return "ErrorCode::kOmahaUpdateIgnoredOverMetered";
    case ErrorCode::kScaledInstallationError:
      return "ErrorCode::kScaledInstallationError";
    case ErrorCode::kNonCriticalUpdateEnrollmentRecovery:
      return "ErrorCode::kNonCriticalUpdateEnrollmentRecovery";
    case ErrorCode::kUpdateIgnoredRollbackVersion:
      return "ErrorCode::kUpdateIgnoredRollbackVersion";
      // Don't add a default case to let the compiler warn about newly added
      // error codes which should be added here.
  }

  return "Unknown error: " + base::NumberToString(static_cast<unsigned>(code));
}

void LogAlertTag(ErrorCode code) {
  code = StripErrorCode(code);
  switch (code) {
    case ErrorCode::kPayloadHashMismatchError:
    case ErrorCode::kPayloadSizeMismatchError:
    case ErrorCode::kPayloadMismatchedType:
      LOG(ERROR) << GenerateAlertTag(kCategoryPayload, kErrorMismatch);
      return;
    case ErrorCode::kSignedDeltaPayloadExpectedError:
      LOG(ERROR) << GenerateAlertTag(kCategoryPayload, kErrorVerification);
      return;
    case ErrorCode::kUnsupportedMajorPayloadVersion:
    case ErrorCode::kUnsupportedMinorPayloadVersion:
      LOG(ERROR) << GenerateAlertTag(kCategoryPayload, kErrorVersion);
      return;
    case ErrorCode::kPayloadTimestampError:
      LOG(ERROR) << GenerateAlertTag(kCategoryPayload, kErrorTimestamp);
      return;
    case ErrorCode::kDownloadInvalidMetadataMagicString:
    case ErrorCode::kDownloadMetadataSignatureError:
    case ErrorCode::kDownloadMetadataSignatureVerificationError:
    case ErrorCode::kDownloadInvalidMetadataSignature:
    case ErrorCode::kDownloadMetadataSignatureMismatch:
    case ErrorCode::kDownloadMetadataSignatureMissingError:
      LOG(ERROR) << GenerateAlertTag(kCategoryDownload, kErrorSignature);
      return;
    case ErrorCode::kDownloadOperationHashVerificationError:
    case ErrorCode::kDownloadOperationHashMismatch:
    case ErrorCode::kDownloadOperationHashMissingError:
    case ErrorCode::kDownloadInvalidMetadataSize:
    case ErrorCode::kDownloadPayloadVerificationError:
    case ErrorCode::kDownloadPayloadPubKeyVerificationError:
      LOG(ERROR) << GenerateAlertTag(kCategoryDownload, kErrorVerification);
      return;
    case ErrorCode::kDownloadSignatureMissingInManifest:
    case ErrorCode::kDownloadManifestParseError:
      LOG(ERROR) << GenerateAlertTag(kCategoryDownload, kErrorManifest);
      return;
    case ErrorCode::kDownloadOperationExecutionError:
      LOG(ERROR) << GenerateAlertTag(kCategoryDownload);
      return;
    case ErrorCode::kVerityCalculationError:
      LOG(ERROR) << GenerateAlertTag(kCategoryVerity);
      return;
    default:
      return;
  }
}
}  // namespace utils
}  // namespace chromeos_update_engine
