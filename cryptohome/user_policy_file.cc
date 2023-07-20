// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/user_policy_file.h"

#include "cryptohome/error/location_utils.h"

namespace cryptohome {

namespace {
using ::cryptohome::error::CryptohomeError;
using ::cryptohome::error::ErrorActionSet;
using ::cryptohome::error::PossibleAction;
using ::hwsec_foundation::status::MakeStatus;
using ::hwsec_foundation::status::OkStatus;
}  // namespace

UserPolicyFile::UserPolicyFile(Platform* platform, const base::FilePath& path)
    : file_(platform, path) {}
UserPolicyFile::~UserPolicyFile() = default;

void UserPolicyFile::UpdateUserPolicy(
    const SerializedUserPolicy& serialized_user_policy) {
  serialized_user_policy_ = serialized_user_policy;
}

CryptohomeStatus UserPolicyFile::StoreInFile() {
  if (!serialized_user_policy_.has_value()) {
    LOG(ERROR) << "Attempting to store an empty policy";
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocUserPolicyStoreEmptyInStoreUserPolicyInFile),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE);
  }
  std::optional<brillo::Blob> flatbuffer_blob =
      serialized_user_policy_.value().Serialize();
  if (!flatbuffer_blob.has_value()) {
    LOG(ERROR) << "Failed to serialize user policies";
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(
            kLocUserPolicySerializeFailedInStoreUserPolicyInFile),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE);
  }
  if (!file_.StoreFile(flatbuffer_blob.value(), kStoreUserPolicyTimer).ok()) {
    LOG(ERROR) << "Failed to store the serialized policies in file";
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocStoreFileFailedInStoreUserPolicyInFile),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE);
  }
  return OkStatus<CryptohomeError>();
}

std::optional<SerializedUserPolicy> UserPolicyFile::GetUserPolicy() {
  return serialized_user_policy_;
}

CryptohomeStatus UserPolicyFile::LoadFromFile() {
  auto file_contents_status = file_.LoadFile(kLoadUserPolicyTimer);
  if (!file_contents_status.ok()) {
    LOG(ERROR) << "Failed to load the user policy information from the file";
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocLoadFileFailedInLoadUserPolicyFromFile),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE);
  }
  std::optional<SerializedUserPolicy> serialized_user_policy_status =
      SerializedUserPolicy::Deserialize(file_contents_status.value());
  if (!serialized_user_policy_status.has_value()) {
    LOG(ERROR) << "Failed to deserialize the user policies from the file";
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocDeserializeFailedInLoadUserPolicyFromFile),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE);
  }
  serialized_user_policy_ = serialized_user_policy_status;
  return OkStatus<CryptohomeError>();
}

}  // namespace cryptohome
