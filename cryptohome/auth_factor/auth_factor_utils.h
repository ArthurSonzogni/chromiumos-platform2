// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_AUTH_FACTOR_AUTH_FACTOR_UTILS_H_
#define CRYPTOHOME_AUTH_FACTOR_AUTH_FACTOR_UTILS_H_

#include <memory>
#include <string>

#include <cryptohome/proto_bindings/auth_factor.pb.h>

#include "cryptohome/auth_factor/auth_factor_metadata.h"
#include "cryptohome/auth_factor/auth_factor_type.h"

namespace cryptohome {

// GetAuthFactorMetadata sets the metadata inferred from the proto. This
// includes the metadata struct, type and label.
bool GetAuthFactorMetadata(const user_data_auth::AuthFactor& auth_factor,
                           AuthFactorMetadata& out_auth_factor_metadata,
                           AuthFactorType& out_auth_factor_type,
                           std::string& out_auth_factor_label);

}  // namespace cryptohome
#endif  // CRYPTOHOME_AUTH_FACTOR_AUTH_FACTOR_UTILS_H_
