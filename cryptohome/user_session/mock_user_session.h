// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_USER_SESSION_MOCK_USER_SESSION_H_
#define CRYPTOHOME_USER_SESSION_MOCK_USER_SESSION_H_

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/files/file_path.h>
#include <brillo/secure_blob.h>
#include <gmock/gmock.h>

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
  MOCK_METHOD(MountStatus,
              MountVault,
              (const std::string&,
               const FileSystemKeyset&,
               const CryptohomeVault::Options&),
              (override));
  MOCK_METHOD(MountStatus, MountEphemeral, (const std::string&), (override));
  MOCK_METHOD(MountStatus, MountGuest, (), (override));
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
  MOCK_METHOD(void, AddCredentials, (const Credentials&), (override));
  MOCK_METHOD(bool, VerifyUser, (const std::string&), (const, override));
  MOCK_METHOD(bool, VerifyCredentials, (const Credentials&), (const, override));
  MOCK_METHOD(void,
              RemoveCredentialVerifierForKeyLabel,
              (const std::string&),
              (override));
  MOCK_METHOD(Pkcs11Token*, GetPkcs11Token, (), (override));
  MOCK_METHOD(std::string, GetUsername, (), (const, override));
  MOCK_METHOD(void,
              PrepareWebAuthnSecret,
              (const brillo::SecureBlob&, const brillo::SecureBlob&),
              (override));
  MOCK_METHOD(bool,
              ResetApplicationContainer,
              (const std::string&),
              (override));

  // Implementation of key_data getter and setter.
  const KeyData& key_data() const override { return key_data_; }
  void set_key_data(KeyData key_data) override {
    key_data_ = std::move(key_data);
  }

  // Implementation of the Add/Has/Get functions for credential verifiers.
  // Functions are implemented "normally" so that tests don't need to manually
  // emulate a map using EXPECT_CALL.
  void AddCredentialVerifier(
      std::unique_ptr<CredentialVerifier> verifier) override {
    const std::string& label = verifier->auth_factor_label();
    label_to_credential_verifier_[label] = std::move(verifier);
  }
  bool HasCredentialVerifier() const override {
    return !label_to_credential_verifier_.empty();
  }
  bool HasCredentialVerifier(const std::string& label) const override {
    return label_to_credential_verifier_.find(label) !=
           label_to_credential_verifier_.end();
  }
  std::vector<const CredentialVerifier*> GetCredentialVerifiers()
      const override {
    std::vector<const CredentialVerifier*> verifiers;
    verifiers.reserve(label_to_credential_verifier_.size());
    for (const auto& [unused, verifier] : label_to_credential_verifier_) {
      verifiers.push_back(verifier.get());
    }
    return verifiers;
  }

 private:
  KeyData key_data_;
  std::map<std::string, std::unique_ptr<CredentialVerifier>>
      label_to_credential_verifier_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_USER_SESSION_MOCK_USER_SESSION_H_
