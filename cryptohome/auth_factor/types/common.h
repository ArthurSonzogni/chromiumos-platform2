// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_AUTH_FACTOR_TYPES_COMMON_H_
#define CRYPTOHOME_AUTH_FACTOR_TYPES_COMMON_H_

#include <optional>
#include <string>

#include <base/containers/span.h>
#include <cryptohome/proto_bindings/auth_factor.pb.h>

#include "cryptohome/auth_blocks/auth_block_type.h"
#include "cryptohome/auth_factor/auth_factor_metadata.h"
#include "cryptohome/auth_factor/auth_factor_type.h"
#include "cryptohome/auth_factor/types/interface.h"

namespace cryptohome {

// Template that wraps a (possibly empty) list of auth block types. This
// template should be used to specify the BlocksType parameter when subclassing
// the typed driver implementation.
//
// Note that we have to define this a little bit weirdly because C++ does not
// support zero-length arrays. So for cases where we specify 1 or more types we
// use an array and wrap it in a span but for the 0 type case we omit the array
// entirely and just use a default (empty) span.
template <AuthBlockType... types>
struct DriverBlockTypes {
  static constexpr AuthBlockType kTypeArray[] = {types...};
  static constexpr base::span<const AuthBlockType> kTypeSpan{kTypeArray};
};
template <>
struct DriverBlockTypes<> {
  static constexpr base::span<const AuthBlockType> kTypeSpan;
};

// Provides an implementation of the portions of the AuthFactorDriver interface
// that are common or reusable by all of the individual driver implementations.
// Most implementations should derive from this class, although it is not
// strictly required.
//
// The class is defined as a template because it needs some type parameters in
// order to control how some of the type-specific interactions work. The
// template arguments are:
//   * MetadataType: the variant type from AuthFactorMetadata::metadata
//   * kType: the auth factor type this driver supports
//   * BlocksType: a DriverBlockTypes<> template specifying the auth block types
//       that this factor can be used with, in highest to lowest priority
template <typename MetadataType, AuthFactorType kType, typename BlocksType>
class TypedAuthFactorDriver : public AuthFactorDriver {
 protected:
  TypedAuthFactorDriver() : AuthFactorDriver(kType) {}

 private:
  base::span<const AuthBlockType> block_types() const override {
    return BlocksType::kTypeSpan;
  }

  std::optional<user_data_auth::AuthFactor> ConvertToProto(
      const std::string& label,
      const AuthFactorMetadata& metadata) const final {
    // Extract the factor-specific metadata and do the typed conversion.
    auto* typed_metadata = std::get_if<MetadataType>(&metadata.metadata);
    if (!typed_metadata) {
      LOG(ERROR)
          << "Cannot convert auth factor to proto, metadata has the wrong type";
      return std::nullopt;
    }
    std::optional<user_data_auth::AuthFactor> proto_factor =
        TypedConvertToProto(metadata.common, *typed_metadata);
    if (!proto_factor) {
      LOG(ERROR) << "Cannot convert auth factor to proto, type-specific "
                    "conversion failed";
      return std::nullopt;
    }
    // If we get here we were able to populate the proto with all of the
    // factor-specific data so now just fillin the common metadata and the
    // label.
    if (proto_factor->common_metadata().lockout_policy() ==
        user_data_auth::LOCKOUT_POLICY_UNKNOWN) {
      proto_factor->mutable_common_metadata()->set_lockout_policy(
          user_data_auth::LOCKOUT_POLICY_NONE);
    }
    proto_factor->set_label(label);
    proto_factor->mutable_common_metadata()->set_chromeos_version_last_updated(
        metadata.common.chromeos_version_last_updated);
    proto_factor->mutable_common_metadata()->set_chrome_version_last_updated(
        metadata.common.chrome_version_last_updated);
    return proto_factor;
  }

  // Type-specific implementation of ConvertToProto that subclasses must
  // implement. Will be called by ConvertToProto if if the given metadata
  // contains the correct type-specific metadata.
  virtual std::optional<user_data_auth::AuthFactor> TypedConvertToProto(
      const CommonAuthFactorMetadata& common,
      const MetadataType& typed_metadata) const = 0;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_AUTH_FACTOR_TYPES_COMMON_H_
