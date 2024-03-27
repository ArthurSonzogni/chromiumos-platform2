// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_AUTH_INPUT_UTILS_H_
#define CRYPTOHOME_AUTH_INPUT_UTILS_H_

#include <optional>

#include <brillo/secure_blob.h>
#include <cryptohome/proto_bindings/auth_factor.pb.h>
#include <libstorage/platform/platform.h>

#include "cryptohome/auth_factor/type.h"
#include "cryptohome/key_objects.h"
#include "cryptohome/username.h"

namespace cryptohome {

// Converts an AuthInput protobuf into the equivalent struct, if possible.
// Returns null if the conversion fails.
//
// The `cryptohome_recovery_ephemeral_pub_key` parameter can be null if it is
// not available.
std::optional<AuthInput> CreateAuthInput(
    libstorage::Platform* platform,
    const user_data_auth::AuthInput& auth_input_proto,
    const Username& username,
    const ObfuscatedUsername& obfuscated_username,
    bool locked_to_single_user,
    const brillo::Blob* cryptohome_recovery_ephemeral_pub_key);

// Infers the `AuthFactorType` that the given `AuthInput` should be used with.
// Returns `nullopt` un unexpected inputs.
std::optional<AuthFactorType> DetermineFactorTypeFromAuthInput(
    const user_data_auth::AuthInput& auth_input_proto);

// Convert a PrepareOutput struct into a PrepareOutput protobuf.
user_data_auth::PrepareOutput PrepareOutputToProto(
    const PrepareOutput& prepare_output);

}  // namespace cryptohome

#endif  // CRYPTOHOME_AUTH_INPUT_UTILS_H_
