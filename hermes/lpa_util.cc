// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hermes/lpa_util.h"

#include <map>
#include <string>

#include <base/strings/stringprintf.h>
#include <brillo/errors/error_codes.h>
#include <brillo/map_utils.h>
#include <chromeos/dbus/service_constants.h>
#include <google-lpa/lpa/card/euicc_card.h>
#include <google-lpa/lpa/core/lpa.h>
#include <google-lpa/lpa/smdx/smdp_client.h>

namespace hermes {

namespace {

constexpr int kOuterErrorBase = 100000;
constexpr int kInnerErrorMax = 1000;

struct InnerError {
  const char* error_code_;
  const char* error_message_;
};

const std::map<int, const char*>& GetOuterErrorMap() {
  static std::map<int, const char*> err_map{
      {0, ""},
      {lpa::core::Lpa::kSetTestModeError, "Lpa SetTestModeError: "},
      {lpa::core::Lpa::kGetEidError, "Lpa GetEid: "},
      {lpa::core::Lpa::kGetInstalledProfilesError,
       "Lpa GetInstalledProfiles: "},
      {lpa::core::Lpa::kEnableProfileError, "Lpa EnableProfile: "},
      {lpa::core::Lpa::kDisableProfileError, "Lpa DisableProfile: "},
      {lpa::core::Lpa::kSetProfileNicknameError, "Lpa SetProfileNickname: "},
      {lpa::core::Lpa::kDeleteProfileError, "Lpa DeleteProfile: "},
      {lpa::core::Lpa::kResetMemoryError, "Lpa ResetMemory: "},
      {lpa::core::Lpa::kDownloadProfileError, "Lpa DownloadProfile: "},
      {lpa::core::Lpa::kGetProfileFromActivationCodeError,
       "Lpa GetProfileFromActivationCode: "},
      {lpa::core::Lpa::kGetDefaultProfileFromSmdpError,
       "Lpa GetDefaultProfileFromSmdp: "},
      {lpa::core::Lpa::kSendNotificationsError, "Lpa SendNotifications: "},
      {lpa::core::Lpa::kGetPendingProfilesError, "Lpa GetPendingProfiles: "},
  };
  return err_map;
}

// NOTE: cellular.Hermes* Tast tests (e.g. HermesConfirmationCode,
// HermesDuplicateInstall, HermesProfilePolicyRules,
// HermesSessionCancelRecovery) substring-match the "<Component> <Method>"
// prefixes below in D-Bus error messages formatted by LpaErrorToBrillo().
// Keep these strings synchronized with those tests if modified.
const std::map<int, const char*>& GetMidErrorMap() {
  static std::map<int, const char*> err_map{
      {0, ""},
      {lpa::card::EuiccCard::kGetEidError, "EuiccCard GetEid: "},
      {lpa::card::EuiccCard::kGetProfilesInfoError,
       "EuiccCard GetProfilesInfo: "},
      {lpa::card::EuiccCard::kEnableProfileError, "EuiccCard EnableProfile: "},
      {lpa::card::EuiccCard::kDisableProfileError,
       "EuiccCard DisableProfile: "},
      {lpa::card::EuiccCard::kDeleteProfileError, "EuiccCard DeleteProfile: "},
      {lpa::card::EuiccCard::kSetNicknameError, "EuiccCard SetNickname: "},
      {lpa::card::EuiccCard::kGetEuiccConfiguredAddressesError,
       "EuiccCard GetEuiccConfiguredAddresses: "},
      {lpa::card::EuiccCard::kSetDefaultSmdpAddressError,
       "EuiccCard SetDefaultSmdpAddress: "},
      {lpa::card::EuiccCard::kGetRatError, "EuiccCard GetRat: "},
      {lpa::card::EuiccCard::kResetMemoryError, "EuiccCard ResetMemory: "},
      {lpa::card::EuiccCard::kGetEuiccChallengeError,
       "EuiccCard GetEuiccChallenge: "},
      {lpa::card::EuiccCard::kGetEuiccInfo1Error, "EuiccCard GetEuiccInfo1: "},
      {lpa::card::EuiccCard::kGetEuiccInfo2Error, "EuiccCard GetEuiccInfo2: "},
      {lpa::card::EuiccCard::kAuthenticateServerError,
       "EuiccCard AuthenticateServer: "},
      {lpa::card::EuiccCard::kPrepareDownloadError,
       "EuiccCard PrepareDownload: "},
      {lpa::card::EuiccCard::kLoadBoundProfilePackageError,
       "EuiccCard LoadBoundProfilePackage: "},
      {lpa::card::EuiccCard::kCancelSessionError, "EuiccCard CancelSession: "},
      {lpa::card::EuiccCard::kListNotificationsError,
       "EuiccCard ListNotifications: "},
      {lpa::card::EuiccCard::kRetrieveNotificationListError,
       "EuiccCard RetrieveNotificationList: "},
      {lpa::card::EuiccCard::kRetrieveNotificationError,
       "EuiccCard RetrieveNotification: "},
      {lpa::card::EuiccCard::kRemoveNotificationFromListError,
       "EuiccCard RemoveNotificationFromList: "},
      {lpa::smdp::SmdpClient::kInitiateAuthenticationError,
       "Smdp InitiateAuthentication: "},
      {lpa::smdp::SmdpClient::kAuthenticateClientError,
       "Smdp AuthenticateClient: "},
      {lpa::smdp::SmdpClient::kGetBoundProfilePackageError,
       "Smdp GetBoundProfilePackage: "},
      {lpa::smdp::SmdpClient::kCancelSessionError, "Smdp CancelSession: "},
      {lpa::smdp::SmdpClient::kHandleNotificationError,
       "Smdp HandleNotification: "},
  };
  return err_map;
}

// LPA-level inner errors (1..10) — plus SmdpClient::kProfileBuild (303),
// emitted by ProfileDownloadTransaction::Authenticate with lpa_mid_code == 0 —
// are kept separate from card/SM-DP+ inner errors so they cannot collide with
// raw eUICC ASN.1 error codes (e.g. LoadBoundProfilePackage ErrorReason = 10,
// installFailedDueToInsufficientMemoryForProfile).
const std::map<int, InnerError>& GetLpaInnerErrorMap() {
  static const std::map<int, InnerError> err_map{
      {lpa::core::Lpa::kWrongState,
       {kErrorWrongState, "Invalid state for requested method"}},
      {lpa::core::Lpa::kIccidNotFound, {kErrorInvalidIccid, "Invalid iccid"}},
      {lpa::core::Lpa::kProfileAlreadyEnabled,
       {kErrorAlreadyEnabled,
        "Requested method provided an already-enabled profile"}},
      {lpa::core::Lpa::kProfileAlreadyDisabled,
       {kErrorAlreadyDisabled, "Requested method provided a disabled profile"}},
      {lpa::core::Lpa::kNeedConfirmationCode,
       {kErrorNeedConfirmationCode, "Need confirmation code"}},
      {lpa::core::Lpa::kInvalidActivationCode,
       {kErrorInvalidActivationCode, "Invalid activation code"}},
      {lpa::core::Lpa::kFailedToSendNotifications,
       {kErrorSendNotificationFailure, "Failed to send notifications"}},
      {lpa::core::Lpa::kNoOpForTestingProfile,
       {kErrorTestProfileInProd, "Non-test mode cannot use test profile"}},
      {lpa::core::Lpa::kPprNotSupported,
       {kErrorUnsupported, "Profile policy rules not allowed"}},
      {lpa::smdp::SmdpClient::kProfileBuild,
       {kErrorMalformedResponse, "Failed to build profile"}},
  };
  return err_map;
}

const std::map<int, InnerError>& GetCardAndSmdpInnerErrorMap() {
  static const std::map<int, InnerError> err_map{
      {lpa::card::EuiccCard::kNoResponses,
       {kErrorNoResponse, "No response from eUICC"}},
      {lpa::card::EuiccCard::kMalformedResponse,
       {kErrorMalformedResponse, "Malformed response from eUICC"}},
      {lpa::card::EuiccCard::kSendApduError,
       {kErrorSendApduFailure, "Failed to send APDU to eUICC"}},
      {lpa::card::EuiccCard::kInvalidIccid,
       {kErrorInvalidIccid, "Invalid iccid"}},
      {lpa::card::EuiccCard::kBadRequest,
       {kErrorBadRequest, "Bad eUICC request"}},
      {lpa::smdp::SmdpClient::kMalformedResponse,
       {kErrorMalformedResponse, "Malformed response"}},
      {lpa::smdp::SmdpClient::kSendHttpsError,
       {kErrorSendHttpsFailure, "Error sending HTTPS"}},
      {lpa::smdp::SmdpClient::kBadNotification,
       {kErrorBadNotification, "Bad notification"}},
      // Per GSMA SGP.22 v2.5 Section 5.2.6, Section 5.6.2 (Table 40), and
      // Section 6.5.1.4, ES9+.GetBoundProfilePackage statusCodeData reports
      // subjectCode "8.2.7" (Confirmation Code) with reasonCode "2.2"
      // (Mandatory Element Missing) when a confirmation code has not yet been
      // supplied (surface kErrorNeedConfirmationCode so Chrome prompts for it),
      // and reasonCode "3.8" (Refused) when a submitted confirmation code is
      // rejected (surface kErrorMalformedResponse so Chrome displays an error
      // on the confirmation code page and allows retry).
      {lpa::smdp::SmdpClient::kConfirmationCodeRefused,
       {kErrorMalformedResponse, "Confirmation code refused by SM-DP+"}},
      {lpa::smdp::SmdpClient::kConfirmationCodeMissing,
       {kErrorNeedConfirmationCode, "Confirmation code missing"}},
  };
  return err_map;
}

const std::map<int, InnerError>& GetInnerErrorMapForAuthenticateServer() {
  constexpr int kPreviouslySeenInvalidActivationCode = 4;
  static std::map<int, InnerError> err_map{
      {lpa::card::EuiccCard::kNoResponses,
       {kErrorNoResponse, "No response from eUICC"}},
      {kPreviouslySeenInvalidActivationCode,
       {kErrorMalformedResponse,
        "Malformed response from eUICC. Invalid activation code may have been "
        "reused"}},
      {lpa::card::EuiccCard::kSendApduError,
       {kErrorSendApduFailure, "Failed to send APDU to eUICC"}},
  };
  return err_map;
}

}  // namespace

brillo::ErrorPtr LpaErrorToBrillo(const base::Location& location, int error) {
  int lpa_inner_code = error % kInnerErrorMax;
  if (lpa_inner_code == lpa::core::Lpa::kNoError) {
    return nullptr;
  }

  int lpa_outer_mod = error % kOuterErrorBase;
  int lpa_outer_code = error - lpa_outer_mod;
  std::string error_message = brillo::GetOrDefault(
      GetOuterErrorMap(), lpa_outer_code, "Lpa UnknownMethod: ");

  int lpa_mid_code = lpa_outer_mod - lpa_inner_code;
  error_message +=
      brillo::GetOrDefault(GetMidErrorMap(), lpa_mid_code, "UnknownMid: ");

  const auto& error_map =
      lpa_mid_code == 0
          ? GetLpaInnerErrorMap()
          : (lpa_mid_code == lpa::card::EuiccCard::kAuthenticateServerError
                 ? GetInnerErrorMapForAuthenticateServer()
                 : GetCardAndSmdpInnerErrorMap());
  const auto& inner_error = brillo::GetOrDefault(
      error_map, lpa_inner_code, {kErrorUnknown, "Unknown error"});

  // NOTE: cellular.Hermes* Tast tests substring-match the "<Component>
  // <Method>" prefix from GetMidErrorMap() and the trailing
  // "(<lpa_inner_code>)" suffix formatted here; keep this message format in
  // sync with those tests.
  error_message +=
      base::StringPrintf("%s (%d)", inner_error.error_message_, lpa_inner_code);

  return brillo::Error::Create(location, brillo::errors::dbus::kDomain,
                               inner_error.error_code_, error_message);
}

}  // namespace hermes
