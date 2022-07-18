// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/error/cryptohome_tpm_error.h"

#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>

#include <cryptohome/proto_bindings/UserDataAuth.pb.h>
#include <libhwsec/error/tpm_retry_action.h>

#include "cryptohome/auth_blocks/tpm_auth_block_utils.h"

namespace cryptohome {

namespace error {

namespace {

using hwsec_foundation::status::NewStatus;
using hwsec_foundation::status::OkStatus;
using hwsec_foundation::status::StatusChain;

// PopulateActionFromRetry is a helper function that converts the libhwsec
// TPMRetryAction into CryptohomeError's Action.
void PopulateActionFromRetry(const hwsec::TPMRetryAction retry,
                             std::set<CryptohomeError::Action>* actions) {
  switch (retry) {
    case hwsec::TPMRetryAction::kCommunication:
    case hwsec::TPMRetryAction::kSession:
    case hwsec::TPMRetryAction::kReboot:
      actions->insert(ErrorAction::kReboot);
      break;
    case hwsec::TPMRetryAction::kLater:
      actions->insert(ErrorAction::kRetry);
      break;
    case hwsec::TPMRetryAction::kDefend:
      actions->insert(ErrorAction::kTpmLockout);
      break;
    case hwsec::TPMRetryAction::kUserAuth:
      actions->insert(ErrorAction::kAuth);
      break;
    case hwsec::TPMRetryAction::kNoRetry:
    case hwsec::TPMRetryAction::kEllipticCurveScalarOutOfRange:
      actions->insert(ErrorAction::kDevCheckUnexpectedState);
      break;
    case hwsec::TPMRetryAction::kNone:
      // No action.
      break;
  }
}

// Converts a normal ErrorLocation into a Unified Error Code that this class'
// |loc_| expects.
CryptohomeError::ErrorLocationPair ErrorLocationToUnified(
    const CryptohomeError::ErrorLocationPair& loc) {
  DCHECK_EQ(loc.location() & (~hwsec::unified_tpm_error::kUnifiedErrorMask), 0);
  return CryptohomeError::ErrorLocationPair(
      loc.location() & hwsec::unified_tpm_error::kUnifiedErrorMask, loc.name());
}

StatusChain<CryptohomeTPMError> FromTPMErrorBase(
    StatusChain<hwsec::TPMErrorBase> status) {
  if (status.ok()) {
    return OkStatus<CryptohomeTPMError>();
  }

  // Status chain currently doesn't offer a way to get the last element of the
  // stack, so we'll need to iterate through it.
  hwsec::TPMErrorBase const* last;
  for (const auto& err : status.const_range()) {
    last = err;
  }

  // Get the unified error code from the last node.
  CryptohomeError::ErrorLocation loc = last->UnifiedErrorCode();

  // Populate the retry actions and status string.
  std::set<CryptohomeError::Action> actual_actions;
  auto retry = status->ToTPMRetryAction();
  PopulateActionFromRetry(retry, &actual_actions);
  std::string loc_str =
      base::StringPrintf("(%s)", status.ToFullString().c_str());

  return NewStatus<CryptohomeTPMError>(
      CryptohomeError::ErrorLocationPair(loc, std::move(loc_str)),
      std::move(actual_actions), retry, std::nullopt);
}

}  // namespace

CryptohomeTPMError::CryptohomeTPMError(
    const ErrorLocationPair& loc,
    const std::set<CryptohomeError::Action>& actions,
    const hwsec::TPMRetryAction retry,
    const std::optional<user_data_auth::CryptohomeErrorCode> ec)
    : CryptohomeCryptoError(
          loc, actions, TpmAuthBlockUtils::TPMRetryActionToCrypto(retry), ec),
      retry_(retry) {}

StatusChain<CryptohomeTPMError> CryptohomeTPMError::MakeStatusTrait::operator()(
    const ErrorLocationPair& loc,
    std::set<CryptohomeError::Action> actions,
    const hwsec::TPMRetryAction retry) {
  PopulateActionFromRetry(retry, &actions);
  auto unified = ErrorLocationToUnified(loc);
  return NewStatus<CryptohomeTPMError>(unified, std::move(actions), retry,
                                       std::nullopt);
}

StatusChain<CryptohomeTPMError> CryptohomeTPMError::MakeStatusTrait::operator()(
    StatusChain<hwsec::TPMErrorBase> status) {
  return FromTPMErrorBase(std::move(status));
}

CryptohomeTPMError::MakeStatusTrait::Unactioned
CryptohomeTPMError::MakeStatusTrait::operator()(
    const ErrorLocationPair& loc,
    const std::set<CryptohomeError::Action>& actions) {
  return CryptohomeTPMError::MakeStatusTrait::Unactioned(loc,
                                                         std::move(actions));
}

CryptohomeTPMError::MakeStatusTrait::Unactioned
CryptohomeTPMError::MakeStatusTrait::operator()(const ErrorLocationPair& loc) {
  return CryptohomeTPMError::MakeStatusTrait::Unactioned(loc, NoErrorAction());
}

CryptohomeTPMError::MakeStatusTrait::Unactioned::Unactioned(
    const ErrorLocationPair& loc,
    const std::set<CryptohomeError::Action>& actions)
    : unified_loc_(ErrorLocationToUnified(loc)), actions_(std::move(actions)) {}

StatusChain<CryptohomeTPMError>
CryptohomeTPMError::MakeStatusTrait::Unactioned::Wrap(
    StatusChain<CryptohomeTPMError> status) && {
  DCHECK(!status.ok());
  return NewStatus<CryptohomeTPMError>(unified_loc_, std::move(actions_),
                                       status->ToTPMRetryAction(), std::nullopt)
      .Wrap(std::move(status));
}

}  // namespace error

}  // namespace cryptohome
