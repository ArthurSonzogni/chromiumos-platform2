// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/error/converter.h"

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>

#include <base/logging.h>
#include <libhwsec-foundation/error/error.h>

#include "cryptohome/error/action.h"
#include "cryptohome/error/reporting.h"

namespace cryptohome {

namespace error {

namespace {

// Functions for mapping the ErrorAction in cryptohome into PossibleAction and
// PrimaryAction that the chromium side can understand.
std::optional<user_data_auth::PrimaryAction> ErrorActionToPrimaryAction(
    ErrorAction err) {
  switch (err) {
    case ErrorAction::kCreateRequired:
      return user_data_auth::PrimaryAction::PRIMARY_CREATE_REQUIRED;
    case ErrorAction::kNotifyOldEncryption:
      return user_data_auth::PrimaryAction::
          PRIMARY_NOTIFY_OLD_ENCRYPTION_POLICY;
    case ErrorAction::kResumePreviousMigration:
      return user_data_auth::PrimaryAction::PRIMARY_RESUME_PREVIOUS_MIGRATION;
    case ErrorAction::kTpmUpdateRequired:
      return user_data_auth::PrimaryAction::PRIMARY_TPM_UDPATE_REQUIRED;
    case ErrorAction::kTpmNeedsReboot:
      return user_data_auth::PrimaryAction::PRIMARY_TPM_NEEDS_REBOOT;
    case ErrorAction::kTpmLockout:
      return user_data_auth::PrimaryAction::PRIMARY_TPM_LOCKOUT;
    case ErrorAction::kIncorrectAuth:
      return user_data_auth::PrimaryAction::PRIMARY_INCORRECT_AUTH;
    default:
      return std::nullopt;
  }
}

std::optional<user_data_auth::PossibleAction> ErrorActionToPossibleAction(
    ErrorAction err) {
  switch (err) {
    case ErrorAction::kRetry:
      return user_data_auth::PossibleAction::POSSIBLY_RETRY;
    case ErrorAction::kReboot:
      return user_data_auth::PossibleAction::POSSIBLY_REBOOT;
    case ErrorAction::kAuth:
      return user_data_auth::PossibleAction::POSSIBLY_AUTH;
    case ErrorAction::kDeleteVault:
      return user_data_auth::PossibleAction::POSSIBLY_DELETE_VAULT;
    case ErrorAction::kPowerwash:
      return user_data_auth::PossibleAction::POSSIBLY_POWERWASH;
    case ErrorAction::kDevCheckUnexpectedState:
      return user_data_auth::PossibleAction::
          POSSIBLY_DEV_CHECK_UNEXPECTED_STATE;
    case ErrorAction::kFatal:
      return user_data_auth::PossibleAction::POSSIBLY_FATAL;
    default:
      return std::nullopt;
  }
}

// Retrieve the ErrorID (aka, the location) from the stack of errors.
// It looks something like this: 5-42-17
std::string ErrorIDFromStack(
    const hwsec_foundation::status::StatusChain<CryptohomeError>& stack) {
  std::string result;
  for (const auto& err : stack.const_range()) {
    if (!result.empty()) {
      result += "-";
    }
    result += std::to_string(err->local_location());
  }
  return result;
}

// Retrieves the recommendation from cryptohome to the caller (Chromium).
// PrimaryAction means that cryptohome is certain that an action will resolve
// the issue, or there's a specific reason why it failed. PossibleAction means
// that cryptohome is uncertain if some actions would resolve the issue but it's
// worth a try anyway.
void ActionsFromStack(
    const hwsec_foundation::status::StatusChain<CryptohomeError>& stack,
    user_data_auth::PrimaryAction* primary,
    std::set<user_data_auth::PossibleAction>* possible) {
  // Check to see if we've any PrimaryAction in the stack.
  *primary = user_data_auth::PrimaryAction::PRIMARY_NONE;
  for (const auto& err : stack.const_range()) {
    // NOTE(b/229708597) The underlying StatusChain will prohibit the iteration
    // of the stack soon, and therefore other users of StatusChain should avoid
    // iterating through the StatusChain without consulting the owner of the
    // bug.
    for (const auto& a : err->local_actions()) {
      auto primary_result = ErrorActionToPrimaryAction(a);
      if (primary_result.has_value()) {
        // The recommended action is a PrimaryAction.
        if (*primary != user_data_auth::PrimaryAction::PRIMARY_NONE) {
          LOG(WARNING) << "Multiple PrimaryAction in an error, got: "
                       << static_cast<int>(*primary) << " and "
                       << static_cast<int>(*primary_result);
        }
        *primary = *primary_result;
      }

      // Obtain the possible actions while we're at it.
      auto possible_result = ErrorActionToPossibleAction(a);
      if (possible_result) {
        possible->insert(*possible_result);
      }
    }
  }

  if (*primary != user_data_auth::PrimaryAction::PRIMARY_NONE) {
    // If we are sure, we'll not propose actions that we're not certain about.
    possible->clear();
    return;
  }

  // If we get here, we're not sure about the failures.
  // Since the PossibleAction(s) are populated as well, we can return.
  return;
}

}  // namespace

user_data_auth::CryptohomeErrorCode LegacyErrorCodeFromStack(
    const hwsec_foundation::status::StatusChain<CryptohomeError>& stack) {
  // Traverse down the stack for the first error
  for (const auto& err : stack.const_range()) {
    // NOTE(b/229708597) The underlying StatusChain will prohibit the iteration
    // of the stack soon, and therefore other users of StatusChain should avoid
    // iterating through the StatusChain without consulting the owner of the
    // bug.
    auto current_legacy_err = err->local_legacy_error();
    if (current_legacy_err) {
      return current_legacy_err.value();
    }
  }
  // There's some form of an error because the original CryptohomeError is not
  // nullptr, therefore, we should leave an unknown error here.
  return user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_UNKNOWN_LEGACY;
}

user_data_auth::CryptohomeErrorInfo CryptohomeErrorToUserDataAuthError(
    const hwsec_foundation::status::StatusChain<CryptohomeError>& err,
    user_data_auth::CryptohomeErrorCode* legacy_ec) {
  user_data_auth::CryptohomeErrorInfo result;
  if (err.ok()) {
    // No error.
    result.set_primary_action(user_data_auth::PrimaryAction::PRIMARY_NO_ERROR);
    if (legacy_ec) {
      *legacy_ec =
          user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_NOT_SET;
    }
    return result;
  }

  // Get the location and recommended actions.
  result.set_error_id(ErrorIDFromStack(err));
  user_data_auth::PrimaryAction primary;
  std::set<user_data_auth::PossibleAction> possible;
  ActionsFromStack(err, &primary, &possible);
  result.set_primary_action(primary);
  for (const auto& a : possible) {
    result.add_possible_actions(a);
  }

  // Get the legacy CryptohomeErrorCode as well.
  if (legacy_ec) {
    *legacy_ec = LegacyErrorCodeFromStack(err);
    if (*legacy_ec ==
        user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_UNKNOWN_LEGACY) {
      LOG(WARNING) << "No legacy error code in error stack for "
                      "CryptohomeErrorToUserDataAuthError: "
                   << result.error_id();
    }
  }

  return result;
}

}  // namespace error

}  // namespace cryptohome
