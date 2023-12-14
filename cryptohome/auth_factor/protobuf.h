// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_AUTH_FACTOR_PROTOBUF_H_
#define CRYPTOHOME_AUTH_FACTOR_PROTOBUF_H_

#include <optional>
#include <string>

#include <cryptohome/proto_bindings/auth_factor.pb.h>

#include "cryptohome/auth_factor/metadata.h"
#include "cryptohome/auth_factor/prepare_purpose.h"
#include "cryptohome/auth_factor/type.h"
#include "cryptohome/features.h"
#include "cryptohome/flatbuffer_schemas/auth_factor.h"

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

// Functions to convert a SerializedLockScreenKnowledgeFactorHashAlgorithm to
// and from the protobuf enum.
LockScreenKnowledgeFactorHashAlgorithm
SerializedKnowledgeFactorAlgorithmToProto(
    const SerializedLockScreenKnowledgeFactorHashAlgorithm& algorithm);
std::optional<SerializedLockScreenKnowledgeFactorHashAlgorithm>
SerializedKnowledgeFactorAlgorithmFromProto(
    const LockScreenKnowledgeFactorHashAlgorithm& algorithm);

// Function to convert an auth factor prepare purpose from the protobuf enum.
std::optional<AuthFactorPreparePurpose> AuthFactorPreparePurposeFromProto(
    user_data_auth::AuthFactorPreparePurpose purpose);

// Populates any relevant fields in an AuthFactor proto with the relevant system
// information (e.g. OS version). Will overwrite any info already populating the
// system information fields, but will not touch any other fields.
void PopulateAuthFactorProtoWithSysinfo(
    user_data_auth::AuthFactor& auth_factor);

// Construct all of the stateless AuthFactor properties (type, label, metadata)
// from an auth factor protobuf.
bool AuthFactorPropertiesFromProto(
    const user_data_auth::AuthFactor& auth_factor,
    const AsyncInitFeatures& features,
    AuthFactorType& out_auth_factor_type,
    std::string& out_auth_factor_label,
    AuthFactorMetadata& out_auth_factor_metadata);

}  // namespace cryptohome

#endif  // CRYPTOHOME_AUTH_FACTOR_PROTOBUF_H_
