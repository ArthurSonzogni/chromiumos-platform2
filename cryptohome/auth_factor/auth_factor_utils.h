// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_AUTH_FACTOR_AUTH_FACTOR_UTILS_H_
#define CRYPTOHOME_AUTH_FACTOR_AUTH_FACTOR_UTILS_H_

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <tuple>

#include <cryptohome/proto_bindings/auth_factor.pb.h>
#include <google/protobuf/repeated_field.h>

#include "cryptohome/auth_factor/auth_factor_manager.h"
#include "cryptohome/auth_factor/auth_factor_map.h"
#include "cryptohome/auth_factor/auth_factor_metadata.h"
#include "cryptohome/auth_factor/auth_factor_prepare_purpose.h"
#include "cryptohome/auth_factor/auth_factor_type.h"
#include "cryptohome/auth_factor_vault_keyset_converter.h"
#include "cryptohome/crypto.h"

namespace cryptohome {

// Functions to convert an auth factor type to and from the protobuf type enum.
//
// Conversion from a proto enum will only fail and return null if given a value
// that does not correspond to any enum value that was known at build time. For
// values which are known, but which can't be mapped onto any AuthFactorType
// value, the kUnspecified value will be returned.
user_data_auth::AuthFactorType AuthFactorTypeToProto(AuthFactorType type);
std::optional<AuthFactorType> AuthFactorTypeFromProto(
    user_data_auth::AuthFactorType type);

// Populates any relevant fields in an AuthFactor proto with the relevant system
// information (e.g. OS version). Will overwrite any info already populating the
// system information fields, but will not touch any other fields.
void PopulateAuthFactorProtoWithSysinfo(
    user_data_auth::AuthFactor& auth_factor);

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
    const AuthBlockUtility& auth_block_utility,
    const std::string& obfuscated_username,
    google::protobuf::RepeatedPtrField<user_data_auth::AuthFactorWithStatus>*
        out_auth_factors);

// Gets AuthFactor for a given user and label. Returns false if the
// corresponding AuthFactor does not exist.
bool LoadUserAuthFactorByLabel(AuthFactorManager* manager,
                               const AuthBlockUtility& auth_block_utility,
                               const std::string& obfuscated_username,
                               const std::string& factor_label,
                               user_data_auth::AuthFactor* out_auth_factor);

// This returns if a given |auth_factor_type| is PinWeaver backed, and thus
// needs a reset secret.
bool NeedsResetSecret(AuthFactorType auth_factor_type);

// Converts to AuthFactorPreparePurpose from the proto enum.
std::optional<AuthFactorPreparePurpose> AuthFactorPreparePurposeFromProto(
    user_data_auth::AuthFactorPreparePurpose purpose);

// Given a keyset converter, factor manager, and platform, load all of the auth
// factors for the given user into an auth factor and key data map.
std::tuple<AuthFactorMap, std::map<std::string, KeyData>> LoadAuthFactorMap(
    const std::string& obfuscated_username,
    Platform& platform,
    AuthFactorVaultKeysetConverter& converter,
    AuthFactorManager& manager);

}  // namespace cryptohome
#endif  // CRYPTOHOME_AUTH_FACTOR_AUTH_FACTOR_UTILS_H_
