// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_factor/auth_factor_utils.h"

#include <memory>

#include "cryptohome/auth_factor/auth_factor.h"
#include "cryptohome/auth_factor/auth_factor_metadata.h"
#include "cryptohome/auth_factor/auth_factor_type.h"

namespace cryptohome {

namespace {
// Set password metadata here, which happens to be empty. For other types of
// factors, there will be more computations involved.
void GetPasswordMetadata(const user_data_auth::AuthFactor& auth_factor,
                         AuthFactorMetadata* auth_factor_metadata) {
  auth_factor_metadata->metadata = PasswordAuthFactorMetadata();
}

}  // namespace

// GetAuthFactorMetadata sets the metadata inferred from the proto. This
// includes the metadata struct and type.
bool GetAuthFactorMetadata(const user_data_auth::AuthFactor& auth_factor,
                           AuthFactorMetadata& auth_factor_metadata,
                           AuthFactorType& auth_factor_type) {
  switch (auth_factor.type()) {
    case user_data_auth::AUTH_FACTOR_TYPE_PASSWORD:
      DCHECK(auth_factor.has_password_metadata());
      GetPasswordMetadata(auth_factor, &auth_factor_metadata);
      auth_factor_type = AuthFactorType::kPassword;
      break;
    default:
      LOG(ERROR) << "Unknown auth factor type " << auth_factor.type();
      return false;
  }
  return true;
}

}  // namespace cryptohome
