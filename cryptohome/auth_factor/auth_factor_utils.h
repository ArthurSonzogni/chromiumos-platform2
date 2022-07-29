// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_AUTH_FACTOR_AUTH_FACTOR_UTILS_H_
#define CRYPTOHOME_AUTH_FACTOR_AUTH_FACTOR_UTILS_H_

#include <memory>
#include <optional>
#include <string>

#include <cryptohome/proto_bindings/auth_factor.pb.h>
#include <google/protobuf/repeated_field.h>

#include "cryptohome/auth_factor/auth_factor_manager.h"
#include "cryptohome/auth_factor/auth_factor_metadata.h"
#include "cryptohome/auth_factor/auth_factor_type.h"

namespace cryptohome {

// GetAuthFactorMetadata sets the metadata inferred from the proto. This
// includes the metadata struct, type and label.
bool GetAuthFactorMetadata(const user_data_auth::AuthFactor& auth_factor,
                           AuthFactorMetadata& out_auth_factor_metadata,
                           AuthFactorType& out_auth_factor_type,
                           std::string& out_auth_factor_label);

// Returns the D-Bus API proto containing the auth factor description.
std::optional<user_data_auth::AuthFactor> GetAuthFactorProto(
    const AuthFactorMetadata& auth_factor_metadata,
    const AuthFactorType& auth_factor_type,
    const std::string& auth_factor_label);

// Populates the D-Bus API proto with all of the auth factor data for a given
// user using the provided factor manager.
void LoadUserAuthFactorProtos(
    AuthFactorManager* manager,
    const std::string& obfuscated_username,
    google::protobuf::RepeatedPtrField<user_data_auth::AuthFactor>*
        out_auth_factors);

// This returns if a given |auth_factor_type| is PinWeaver backed, and thus
// needs a reset secret.
bool NeedsResetSecret(AuthFactorType auth_factor_type);

}  // namespace cryptohome
#endif  // CRYPTOHOME_AUTH_FACTOR_AUTH_FACTOR_UTILS_H_
