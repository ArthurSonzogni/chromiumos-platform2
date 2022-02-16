// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/error/cryptohome_tpm_error.h"

#include <memory>
#include <optional>
#include <set>
#include <utility>

#include <cryptohome/proto_bindings/UserDataAuth.pb.h>
#include <libhwsec/error/tpm_retry_action.h>

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
      actions->insert(ErrorAction::kDevCheckUnexpectedState);
      break;
    case hwsec::TPMRetryAction::kNone:
      // No action.
      break;
  }
}

// Converts a normal ErrorLocation into a Unified Error Code that this class'
// |loc_| expects.
CryptohomeError::ErrorLocation ErrorLocationToUnified(
    CryptohomeError::ErrorLocation loc) {
  DCHECK_EQ(loc & (~hwsec::unified_tpm_error::kUnifiedErrorMask), 0);
  return (loc & hwsec::unified_tpm_error::kUnifiedErrorMask);
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

  // Populate the retry actions.
  std::set<CryptohomeError::Action> actual_actions;
  auto retry = last->ToTPMRetryAction();
  PopulateActionFromRetry(retry, &actual_actions);

  // Get the unified error code from the last node.
  CryptohomeError::ErrorLocation loc = last->UnifiedErrorCode();
  return NewStatus<CryptohomeTPMError>(loc, std::move(actual_actions), retry,
                                       std::move(status), std::nullopt);
}

}  // namespace

CryptohomeTPMError::CryptohomeTPMError(
    const CryptohomeError::ErrorLocation loc,
    const std::set<CryptohomeError::Action>& actions,
    const hwsec::TPMRetryAction retry,
    std::optional<hwsec_foundation::status::StatusChain<hwsec::TPMErrorBase>>
        tpm_error,
    const std::optional<user_data_auth::CryptohomeErrorCode> ec)
    : CryptohomeError(loc, actions, ec),
      retry_(retry),
      tpm_error_(std::move(tpm_error)) {}

StatusChain<CryptohomeTPMError> CryptohomeTPMError::MakeStatusTrait::operator()(
    CryptohomeError::ErrorLocation loc,
    std::set<CryptohomeError::Action> actions,
    hwsec::TPMRetryAction retry) {
  PopulateActionFromRetry(retry, &actions);
  CryptohomeError::ErrorLocation unified = ErrorLocationToUnified(loc);
  return NewStatus<CryptohomeTPMError>(unified, std::move(actions), retry,
                                       std::nullopt, std::nullopt);
}

StatusChain<CryptohomeTPMError> CryptohomeTPMError::MakeStatusTrait::operator()(
    StatusChain<hwsec::TPMErrorBase> status) {
  return FromTPMErrorBase(std::move(status));
}

CryptohomeTPMError::MakeStatusTrait::Unactioned
CryptohomeTPMError::MakeStatusTrait::operator()(
    CryptohomeError::ErrorLocation loc,
    std::set<CryptohomeError::Action> actions) {
  return CryptohomeTPMError::MakeStatusTrait::Unactioned(loc, actions);
}

CryptohomeTPMError::MakeStatusTrait::Unactioned::Unactioned(
    CryptohomeError::ErrorLocation loc,
    std::set<CryptohomeError::Action> actions)
    : unified_loc_(ErrorLocationToUnified(loc)), actions_(std::move(actions)) {}

StatusChain<CryptohomeTPMError>
CryptohomeTPMError::MakeStatusTrait::Unactioned::Wrap(
    StatusChain<CryptohomeTPMError> status) && {
  DCHECK(!status.ok());
  return NewStatus<CryptohomeTPMError>(unified_loc_, std::move(actions_),
                                       status->ToTPMRetryAction(), std::nullopt,
                                       std::nullopt)
      .Wrap(std::move(status));
}

}  // namespace error

}  // namespace cryptohome
