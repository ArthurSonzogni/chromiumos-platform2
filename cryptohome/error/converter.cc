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

user_data_auth::PrimaryAction PrimaryActionToProto(PrimaryAction action) {
  switch (action) {
    case PrimaryAction::kTpmUpdateRequired:
      return user_data_auth::PrimaryAction::PRIMARY_TPM_UDPATE_REQUIRED;
    case PrimaryAction::kTpmLockout:
      return user_data_auth::PrimaryAction::PRIMARY_TPM_LOCKOUT;
    case PrimaryAction::kIncorrectAuth:
      return user_data_auth::PrimaryAction::PRIMARY_INCORRECT_AUTH;
    case PrimaryAction::kLeLockedOut:
      return user_data_auth::PrimaryAction::PRIMARY_LE_LOCKED_OUT;
  }
}

user_data_auth::PossibleAction PossibleActionToProto(PossibleAction action) {
  switch (action) {
    case PossibleAction::kRetry:
      return user_data_auth::PossibleAction::POSSIBLY_RETRY;
    case PossibleAction::kReboot:
      return user_data_auth::PossibleAction::POSSIBLY_REBOOT;
    case PossibleAction::kAuth:
      return user_data_auth::PossibleAction::POSSIBLY_AUTH;
    case PossibleAction::kDeleteVault:
      return user_data_auth::PossibleAction::POSSIBLY_DELETE_VAULT;
    case PossibleAction::kPowerwash:
      return user_data_auth::PossibleAction::POSSIBLY_POWERWASH;
    case PossibleAction::kDevCheckUnexpectedState:
      return user_data_auth::PossibleAction::
          POSSIBLY_DEV_CHECK_UNEXPECTED_STATE;
    case PossibleAction::kFatal:
      return user_data_auth::PossibleAction::POSSIBLY_FATAL;
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
// worth a try anyway. Currently when there are multiple primary actions in the
// stack, the first one (upper-layer) is used. This behavior might be changed in
// the future.
// TODO(b/275676828): Determine best way to choose one primary action from the
// stack.
void ActionsFromStack(
    const hwsec_foundation::status::StatusChain<CryptohomeError>& stack,
    std::optional<PrimaryAction>& primary,
    PossibleActions& possible) {
  // Check to see if we've any PrimaryAction in the stack.
  for (const auto& err : stack.const_range()) {
    // NOTE(b/229708597) The underlying StatusChain will prohibit the iteration
    // of the stack soon, and therefore other users of StatusChain should avoid
    // iterating through the StatusChain without consulting the owner of the
    // bug.
    if (std::holds_alternative<PrimaryAction>(err->local_actions())) {
      primary = std::get<PrimaryAction>(err->local_actions());
      break;
    }
    possible |= std::get<PossibleActions>(err->local_actions());
  }

  if (primary.has_value()) {
    // If we are sure, we'll not propose actions that we're not certain about.
    possible.reset();
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
  std::optional<PrimaryAction> primary;
  PossibleActions possible;
  ActionsFromStack(err, primary, possible);
  if (primary.has_value()) {
    result.set_primary_action(PrimaryActionToProto(*primary));
  } else {
    result.set_primary_action(user_data_auth::PrimaryAction::PRIMARY_NONE);
    for (size_t i = 0; i < possible.size(); ++i) {
      if (possible[i]) {
        result.add_possible_actions(
            PossibleActionToProto(static_cast<PossibleAction>(i)));
      }
    }
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
