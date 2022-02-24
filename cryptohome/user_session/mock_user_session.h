// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_USER_SESSION_MOCK_USER_SESSION_H_
#define CRYPTOHOME_USER_SESSION_MOCK_USER_SESSION_H_

#include <memory>
#include <string>

#include <base/files/file_path.h>
#include <brillo/secure_blob.h>
#include <gmock/gmock.h>

#include "cryptohome/auth_session.h"
#include "cryptohome/cleanup/user_oldest_activity_timestamp_manager.h"
#include "cryptohome/credential_verifier.h"
#include "cryptohome/credentials.h"
#include "cryptohome/keyset_management.h"
#include "cryptohome/pkcs11/pkcs11_token.h"
#include "cryptohome/pkcs11/pkcs11_token_factory.h"
#include "cryptohome/storage/cryptohome_vault.h"
#include "cryptohome/storage/homedirs.h"
#include "cryptohome/storage/mount.h"
#include "cryptohome/user_session/user_session.h"

namespace cryptohome {

class MockUserSession : public UserSession {
 public:
  MockUserSession() = default;

  MOCK_METHOD(bool, IsActive, (), (const, override));
  MOCK_METHOD(bool, IsEphemeral, (), (const, override));
  MOCK_METHOD(bool, OwnsMountPoint, (const base::FilePath&), (const, override));
  MOCK_METHOD(
      bool,
      MigrateVault,
      (const dircrypto_data_migrator::MigrationHelper::ProgressCallback&,
       MigrationType),
      (override));
  MOCK_METHOD(MountError,
              MountVault,
              (const std::string,
               const FileSystemKeyset&,
               const CryptohomeVault::Options&),
              (override));
  MOCK_METHOD(MountError, MountEphemeral, (const std::string), (override));
  MOCK_METHOD(MountError, MountGuest, (), (override));
  MOCK_METHOD(bool, Unmount, (), (override));
  MOCK_METHOD(base::Value, GetStatus, (), (const, override));
  MOCK_METHOD(std::unique_ptr<brillo::SecureBlob>,
              GetWebAuthnSecret,
              (),
              (override));
  MOCK_METHOD(const brillo::SecureBlob&,
              GetWebAuthnSecretHash,
              (),
              (const, override));
  MOCK_METHOD(std::unique_ptr<brillo::SecureBlob>,
              GetHibernateSecret,
              (),
              (override));
  MOCK_METHOD(bool, SetCredentials, (const Credentials&), (override));
  MOCK_METHOD(void, SetCredentials, (AuthSession*), (override));
  MOCK_METHOD(bool, VerifyUser, (const std::string&), (const, override));
  MOCK_METHOD(bool, VerifyCredentials, (const Credentials&), (const, override));
  MOCK_METHOD(const KeyData&, key_data, (), (const, override));
  MOCK_METHOD(Pkcs11Token*, GetPkcs11Token, (), (override));
  MOCK_METHOD(std::string, GetUsername, (), (const, override));
  MOCK_METHOD(void,
              PrepareWebAuthnSecret,
              (const brillo::SecureBlob&, const brillo::SecureBlob&),
              (override));

 private:
  ~MockUserSession() override = default;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_USER_SESSION_MOCK_USER_SESSION_H_
