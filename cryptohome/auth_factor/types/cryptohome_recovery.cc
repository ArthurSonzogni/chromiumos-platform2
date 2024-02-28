// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_factor/types/cryptohome_recovery.h"

#include <base/time/time.h>
#include <libhwsec-foundation/status/status_chain.h>

#include "cryptohome/auth_blocks/cryptohome_recovery_auth_block.h"
#include "cryptohome/auth_factor/auth_factor.h"
#include "cryptohome/auth_factor/label_arity.h"
#include "cryptohome/auth_factor/metadata.h"
#include "cryptohome/auth_factor/storage_type.h"
#include "cryptohome/auth_factor/type.h"
#include "cryptohome/error/cryptohome_error.h"
#include "cryptohome/error/location_utils.h"
#include "cryptohome/error/locations.h"
#include "cryptohome/filesystem_layout.h"
#include "cryptohome/flatbuffer_schemas/auth_block_state.h"
#include "cryptohome/flatbuffer_schemas/auth_factor.h"

namespace cryptohome {
namespace {

using ::cryptohome::error::ErrorActionSet;
using ::cryptohome::error::PossibleAction;
using error::CryptohomeError;
using ::hwsec_foundation::status::MakeStatus;

}  // namespace

bool CryptohomeRecoveryAuthFactorDriver::IsSupportedByHardware() const {
  return CryptohomeRecoveryAuthBlock::IsSupported(*crypto_).ok();
}

bool CryptohomeRecoveryAuthFactorDriver::NeedsResetSecret() const {
  return false;
}

AuthFactorLabelArity
CryptohomeRecoveryAuthFactorDriver::GetAuthFactorLabelArity() const {
  return AuthFactorLabelArity::kSingle;
}

std::optional<user_data_auth::AuthFactor>
CryptohomeRecoveryAuthFactorDriver::TypedConvertToProto(
    const CommonMetadata& common,
    const CryptohomeRecoveryMetadata& typed_metadata) const {
  user_data_auth::AuthFactor proto;
  proto.set_type(user_data_auth::AUTH_FACTOR_TYPE_CRYPTOHOME_RECOVERY);
  proto.mutable_cryptohome_recovery_metadata()->set_mediator_pub_key(
      brillo::BlobToString(typed_metadata.mediator_pub_key));
  return proto;
}

bool CryptohomeRecoveryAuthFactorDriver::IsDelaySupported() const {
  return true;
}

CryptohomeStatusOr<base::TimeDelta>
CryptohomeRecoveryAuthFactorDriver::GetFactorDelay(
    const ObfuscatedUsername& username, const AuthFactor& factor) const {
  // Do all the error checks to make sure the input is useful.
  if (factor.type() != type()) {
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocAuthFactorRecoveryGetFactorDelayWrongFactorType),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
  }
  auto* state = std::get_if<CryptohomeRecoveryAuthBlockState>(
      &(factor.auth_block_state().state));
  if (!state) {
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(
            kLocAuthFactorRecoveryGetFactorDelayInvalidBlockState),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
  }

  // CryptohomeRecovery factor is either locked with max time delay or not
  // delayed.
  if (platform_->FileExists(GetRecoveryFactorLockPath())) {
    return base::TimeDelta::Max();
  } else {
    return base::TimeDelta();
  }
}

}  // namespace cryptohome
