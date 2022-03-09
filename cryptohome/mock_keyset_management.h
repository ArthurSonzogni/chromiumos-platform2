// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_MOCK_KEYSET_MANAGEMENT_H_
#define CRYPTOHOME_MOCK_KEYSET_MANAGEMENT_H_

#include "cryptohome/keyset_management.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <brillo/secure_blob.h>
#include <dbus/cryptohome/dbus-constants.h>
#include <gmock/gmock.h>

#include "cryptohome/auth_blocks/auth_block_state.h"
#include "cryptohome/credentials.h"
#include "cryptohome/key_objects.h"
#include "cryptohome/storage/file_system_keyset.h"
#include "cryptohome/storage/mount.h"

namespace cryptohome {
class VaultKeyset;
class HomeDirs;

typedef std::map<std::string, KeyData> KeyLabelMap;

class MockKeysetManagement : public KeysetManagement {
 public:
  MockKeysetManagement() = default;
  virtual ~MockKeysetManagement() = default;

  MOCK_METHOD(bool, AreCredentialsValid, (const Credentials&), (override));
  MOCK_METHOD(bool,
              Migrate,
              (const VaultKeyset&, const Credentials&),
              (override));
  MOCK_METHOD(std::unique_ptr<VaultKeyset>,
              GetValidKeyset,
              (const Credentials&, MountError*),
              (override));
  MOCK_METHOD(std::unique_ptr<VaultKeyset>,
              GetVaultKeyset,
              (const std::string&, const std::string&),
              (const, override));
  MOCK_METHOD(bool,
              GetVaultKeysets,
              (const std::string&, std::vector<int>*),
              (const, override));
  MOCK_METHOD(bool,
              GetVaultKeysetLabels,
              (const std::string&, std::vector<std::string>*),
              (const, override));
  MOCK_METHOD(bool,
              GetVaultKeysetLabelsAndData,
              (const std::string&, KeyLabelMap*),
              (const, override));
  MOCK_METHOD(std::unique_ptr<VaultKeyset>,
              AddInitialKeyset,
              (const Credentials&, const FileSystemKeyset&),
              (override));
  MOCK_METHOD(CryptohomeErrorCode,
              AddWrappedResetSeedIfMissing,
              (VaultKeyset * vault_keyset, const Credentials& credentials),
              (override));
  MOCK_METHOD(CryptohomeErrorCode,
              AddKeyset,
              (const Credentials&, const VaultKeyset&, bool),
              (override));
  MOCK_METHOD(CryptohomeErrorCode,
              UpdateKeyset,
              (const Credentials&, const VaultKeyset&),
              (override));
  MOCK_METHOD(CryptohomeErrorCode,
              RemoveKeyset,
              (const Credentials&, const KeyData&),
              (override));
  MOCK_METHOD(bool, ForceRemoveKeyset, (const std::string&, int), (override));
  MOCK_METHOD(bool, MoveKeyset, (const std::string&, int, int), (override));
  MOCK_METHOD(void, RemoveLECredentials, (const std::string&), (override));
  MOCK_METHOD(bool, UserExists, (const std::string&), (override));
  MOCK_METHOD(brillo::SecureBlob,
              GetPublicMountPassKey,
              (const std::string&),
              (override));
  MOCK_METHOD(base::Time,
              GetKeysetBoundTimestamp,
              (const std::string&),
              (override));
  MOCK_METHOD(void,
              CleanupPerIndexTimestampFiles,
              (const std::string&),
              (override));
  MOCK_METHOD(bool,
              ReSaveKeysetIfNeeded,
              (const Credentials& credentials, VaultKeyset* keyset),
              (const, override));
  MOCK_METHOD(std::unique_ptr<VaultKeyset>,
              GetValidKeysetWithKeyBlobs,
              (const std::string&,
               KeyBlobs,
               const base::Optional<std::string>&,
               MountError*),
              (override));
  MOCK_METHOD(CryptohomeErrorCode,
              AddKeysetWithKeyBlobs,
              (const std::string&,
               const KeyData&,
               const VaultKeyset&,
               KeyBlobs,
               std::unique_ptr<AuthBlockState>,
               bool clobber),
              (override));
  MOCK_METHOD(std::unique_ptr<VaultKeyset>,
              AddInitialKeysetWithKeyBlobs,
              (const std::string&,
               const KeyData&,
               const SerializedVaultKeyset_SignatureChallengeInfo&,
               const FileSystemKeyset&,
               KeyBlobs,
               std::unique_ptr<AuthBlockState>),
              (override));
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_MOCK_KEYSET_MANAGEMENT_H_
