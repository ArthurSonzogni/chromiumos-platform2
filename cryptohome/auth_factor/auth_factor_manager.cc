// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_factor/auth_factor_manager.h"

#include <sys/stat.h>

#include <string>
#include <utility>

#include <absl/types/variant.h>
#include <base/files/file_path.h>
#include <base/check.h>
#include <base/logging.h>
#include <base/optional.h>
#include <brillo/secure_blob.h>
#include <flatbuffers/flatbuffers.h>

#include "cryptohome/auth_factor/auth_factor.h"
#include "cryptohome/auth_factor/auth_factor_metadata.h"
#include "cryptohome/auth_factor/auth_factor_type.h"
#include "cryptohome/auth_factor_generated.h"
#include "cryptohome/filesystem_layout.h"
#include "cryptohome/flatbuffer_secure_allocator_bridge.h"
#include "cryptohome/platform.h"

using brillo::Blob;

namespace cryptohome {

namespace {

// Use rw------- for the auth factor files.
constexpr mode_t kAuthFactorFilePermissions = 0600;

constexpr int kFlatbufferAllocatorInitialSize = 4096;

// Note: The string values in this constant must stay stable, as they're used in
// file names.
constexpr std::pair<AuthFactorType, const char*> kAuthFactorTypeStrings[] = {
    {AuthFactorType::kPassword, "password"},
};

// Converts the auth factor type enum into a string.
std::string GetAuthFactorTypeString(AuthFactorType type) {
  for (const auto& type_and_string : kAuthFactorTypeStrings) {
    if (type_and_string.first == type) {
      return type_and_string.second;
    }
  }
  return std::string();
}

// Serializes the password metadata into the given flatbuffer builder. Returns
// the flatbuffer offset, to be used for building the outer table.
flatbuffers::Offset<SerializedPasswordMetadata> SerializeMetadataToOffset(
    const PasswordAuthFactorMetadata& password_metadata,
    flatbuffers::FlatBufferBuilder* builder) {
  SerializedPasswordMetadataBuilder metadata_builder(*builder);
  return metadata_builder.Finish();
}

// Serializes the password metadata into the given flatbuffer builder. Returns
// the flatbuffer offset, to be used for building the outer table.
flatbuffers::Offset<void> SerializeMetadataToOffset(
    const AuthFactorMetadata& metadata,
    flatbuffers::FlatBufferBuilder* builder,
    SerializedAuthFactorMetadata* metadata_type) {
  if (const auto* password_metadata =
          absl::get_if<PasswordAuthFactorMetadata>(&metadata.metadata)) {
    *metadata_type = SerializedAuthFactorMetadata::SerializedPasswordMetadata;
    return SerializeMetadataToOffset(*password_metadata, builder).Union();
  }
  LOG(ERROR) << "Missing or unexpected auth factor metadata: "
             << metadata.metadata.index();
  return flatbuffers::Offset<void>();
}

// Serializes the auth factor into a flatbuffer blob. Returns null on failure.
base::Optional<Blob> SerializeAuthFactor(const AuthFactor& auth_factor) {
  FlatbufferSecureAllocatorBridge allocator;
  flatbuffers::FlatBufferBuilder builder(kFlatbufferAllocatorInitialSize,
                                         &allocator);

  auto auth_block_state_offset =
      auth_factor.auth_block_state().value().SerializeToOffset(&builder);
  if (auth_block_state_offset.IsNull()) {
    LOG(ERROR) << "Failed to serialize auth block state";
    return base::nullopt;
  }

  SerializedAuthFactorMetadata metadata_type =
      SerializedAuthFactorMetadata::NONE;
  flatbuffers::Offset<void> metadata_offset = SerializeMetadataToOffset(
      auth_factor.metadata().value(), &builder, &metadata_type);
  if (metadata_offset.IsNull()) {
    LOG(ERROR) << "Failed to serialize metadata";
    return base::nullopt;
  }

  SerializedAuthFactorBuilder auth_factor_builder(builder);
  auth_factor_builder.add_auth_block_state(auth_block_state_offset);
  auth_factor_builder.add_metadata(metadata_offset);
  auth_factor_builder.add_metadata_type(metadata_type);
  auto auth_factor_offset = auth_factor_builder.Finish();

  builder.Finish(auth_factor_offset);
  return Blob(builder.GetBufferPointer(),
              builder.GetBufferPointer() + builder.GetSize());
}

}  // namespace

AuthFactorManager::AuthFactorManager(Platform* platform) : platform_(platform) {
  DCHECK(platform_);
}

AuthFactorManager::~AuthFactorManager() = default;

bool AuthFactorManager::SaveAuthFactor(const std::string& obfuscated_username,
                                       const AuthFactor& auth_factor) {
  // TODO(b:208351356): Remove the DCHECKs after these fields are made
  // non-optional.
  DCHECK(auth_factor.type().has_value());
  DCHECK(auth_factor.label().has_value());
  DCHECK(auth_factor.metadata().has_value());
  DCHECK(auth_factor.label().has_value());

  const std::string type_string =
      GetAuthFactorTypeString(auth_factor.type().value());
  if (type_string.empty()) {
    LOG(ERROR) << "Failed to convert auth factor type "
               << static_cast<int>(auth_factor.type().value())
               << " for factor called " << auth_factor.label().value();
    return false;
  }

  base::Optional<Blob> flatbuffer = SerializeAuthFactor(auth_factor);
  if (!flatbuffer.has_value()) {
    LOG(ERROR) << "Failed to serialize auth factor "
               << auth_factor.label().value() << " of type " << type_string;
    return false;
  }

  base::FilePath file_path = AuthFactorPath(obfuscated_username, type_string,
                                            auth_factor.label().value());
  if (!platform_->WriteFileAtomicDurable(file_path, flatbuffer.value(),
                                         kAuthFactorFilePermissions)) {
    LOG(ERROR) << "Failed to persist auth factor "
               << auth_factor.label().value() << " of type " << type_string
               << " for " << obfuscated_username;
    return false;
  }

  return true;
}

}  // namespace cryptohome
