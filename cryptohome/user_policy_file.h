// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_USER_POLICY_FILE_H_
#define CRYPTOHOME_USER_POLICY_FILE_H_

#include "cryptohome/auth_factor/types/manager.h"
#include "cryptohome/flatbuffer_file.h"
#include "cryptohome/flatbuffer_schemas/user_policy.h"
#include "cryptohome/platform.h"

namespace cryptohome {

class UserPolicyFile final {
 public:
  UserPolicyFile(Platform* platform, const base::FilePath& path);

  UserPolicyFile(const UserPolicyFile&) = delete;
  UserPolicyFile& operator=(const UserPolicyFile&) = delete;

  ~UserPolicyFile();

  // Serializes and stores the user policy in |file_|.
  CryptohomeStatus StoreInFile();
  // Reads the serialized user policy from |file_|.
  CryptohomeStatus LoadFromFile();

  // Updates the user policy. Notice that in order for the update to become
  // permanent, the |StoreUserPolicyInFile| should be called.
  void UpdateUserPolicy(const SerializedUserPolicy& serialized_user_policy);
  // Gets the user policy.
  std::optional<SerializedUserPolicy> GetUserPolicy() const;

 private:
  const FlatbufferFile file_;
  std::optional<SerializedUserPolicy> serialized_user_policy_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_USER_POLICY_FILE_H_
