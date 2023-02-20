// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_factor/auth_factor.h"

#include <memory>
#include <utility>

#include <base/logging.h>
#include <cryptohome/proto_bindings/UserDataAuth.pb.h>

#include "cryptohome/auth_blocks/auth_block_utility.h"
#include "cryptohome/error/converter.h"
#include "cryptohome/error/cryptohome_error.h"
#include "cryptohome/error/location_utils.h"
#include "cryptohome/key_objects.h"

using cryptohome::error::CryptohomeError;
using hwsec_foundation::status::MakeStatus;
using hwsec_foundation::status::OkStatus;

namespace cryptohome {

AuthFactor::AuthFactor(AuthFactorType type,
                       const std::string& label,
                       const AuthFactorMetadata& metadata,
                       const AuthBlockState& auth_block_state)
    : type_(type),
      label_(label),
      metadata_(metadata),
      auth_block_state_(auth_block_state) {}

CryptohomeStatus AuthFactor::PrepareForRemoval(
    AuthBlockUtility* auth_block_utility) const {
  CryptohomeStatus error =
      auth_block_utility->PrepareAuthBlockForRemoval(auth_block_state_);
  if (!error.ok()) {
    LOG(ERROR) << "Prepare auth factor for removal failed: error " << error;
    return MakeStatus<CryptohomeError>(
               CRYPTOHOME_ERR_LOC(kLocAuthFactorPrepareForRemovalFailed))
        .Wrap(std::move(error));
  }
  return OkStatus<CryptohomeError>();
}

}  // namespace cryptohome
