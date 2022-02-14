// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/user_session.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/logging.h>
#include <base/memory/ref_counted.h>
#include <base/values.h>

#include <brillo/cryptohome.h>
#include <cryptohome/scrypt_verifier.h>

#include "cryptohome/cleanup/user_oldest_activity_timestamp_manager.h"
#include "cryptohome/credentials.h"
#include "cryptohome/crypto/hmac.h"
#include "cryptohome/crypto/sha.h"
#include "cryptohome/filesystem_layout.h"
#include "cryptohome/keyset_management.h"
#include "cryptohome/pkcs11/pkcs11_token.h"
#include "cryptohome/pkcs11/pkcs11_token_factory.h"
#include "cryptohome/storage/cryptohome_vault.h"
#include "cryptohome/storage/mount.h"

using brillo::cryptohome::home::kGuestUserName;
using brillo::cryptohome::home::SanitizeUserName;
using brillo::cryptohome::home::SanitizeUserNameWithSalt;

namespace cryptohome {

// Message to use when generating a secret for WebAuthn.
constexpr char kWebAuthnSecretHmacMessage[] = "AuthTimeWebAuthnSecret";

// Message to use when generating a secret for hibernate.
constexpr char kHibernateSecretHmacMessage[] = "AuthTimeHibernateSecret";

UserSession::UserSession() {}
UserSession::~UserSession() {}
UserSession::UserSession(
    HomeDirs* homedirs,
    KeysetManagement* keyset_management,
    UserOldestActivityTimestampManager* user_activity_timestamp_manager,
    Pkcs11TokenFactory* pkcs11_token_factory,
    const brillo::SecureBlob& salt,
    const scoped_refptr<Mount> mount)
    : homedirs_(homedirs),
      keyset_management_(keyset_management),
      user_activity_timestamp_manager_(user_activity_timestamp_manager),
      pkcs11_token_factory_(pkcs11_token_factory),
      system_salt_(salt),
      mount_(mount) {}

MountError UserSession::MountVault(
    const std::string username,
    const FileSystemKeyset& fs_keyset,
    const CryptohomeVault::Options& vault_options) {
  MountError error = MOUNT_ERROR_NONE;

  error = mount_->MountCryptohome(username, fs_keyset, vault_options);
  if (error != MOUNT_ERROR_NONE) {
    return error;
  }

  obfuscated_username_ = SanitizeUserName(username);
  user_activity_timestamp_manager_->UpdateTimestamp(obfuscated_username_,
                                                    base::TimeDelta());
  pkcs11_token_ = pkcs11_token_factory_->New(
      username, homedirs_->GetChapsTokenDir(username), fs_keyset.chaps_key());

  PrepareWebAuthnSecret(fs_keyset.Key().fek, fs_keyset.Key().fnek);
  PrepareHibernateSecret(fs_keyset.Key().fek, fs_keyset.Key().fnek);

  return MOUNT_ERROR_NONE;
}

MountError UserSession::MountEphemeral(const std::string username) {
  if (homedirs_->IsOrWillBeOwner(username)) {
    return MOUNT_ERROR_EPHEMERAL_MOUNT_BY_OWNER;
  }

  MountError error = mount_->MountEphemeralCryptohome(username);
  if (error == MOUNT_ERROR_NONE) {
    pkcs11_token_ = pkcs11_token_factory_->New(
        username_, homedirs_->GetChapsTokenDir(username_),
        brillo::SecureBlob());
  }

  return error;
}

MountError UserSession::MountGuest() {
  return mount_->MountEphemeralCryptohome(kGuestUserName);
}

bool UserSession::Unmount() {
  if (pkcs11_token_) {
    pkcs11_token_->Remove();
    pkcs11_token_.reset();
  }
  if (mount_->IsNonEphemeralMounted()) {
    user_activity_timestamp_manager_->UpdateTimestamp(obfuscated_username_,
                                                      base::TimeDelta());
  }
  return mount_->UnmountCryptohome();
}

base::Value UserSession::GetStatus() const {
  base::Value dv(base::Value::Type::DICTIONARY);
  std::string user = SanitizeUserNameWithSalt(username_, system_salt_);
  base::Value keysets(base::Value::Type::LIST);
  std::vector<int> key_indices;
  if (user.length() &&
      keyset_management_->GetVaultKeysets(user, &key_indices)) {
    for (auto key_index : key_indices) {
      base::Value keyset_dict(base::Value::Type::DICTIONARY);
      std::unique_ptr<VaultKeyset> keyset(
          keyset_management_->LoadVaultKeysetForUser(user, key_index));
      if (keyset.get()) {
        bool tpm = keyset->GetFlags() & SerializedVaultKeyset::TPM_WRAPPED;
        bool scrypt =
            keyset->GetFlags() & SerializedVaultKeyset::SCRYPT_WRAPPED;
        keyset_dict.SetBoolKey("tpm", tpm);
        keyset_dict.SetBoolKey("scrypt", scrypt);
        keyset_dict.SetBoolKey("ok", true);
        if (keyset->HasKeyData()) {
          keyset_dict.SetStringKey("label", keyset->GetKeyData().label());
        }
      } else {
        keyset_dict.SetBoolKey("ok", false);
      }
      keyset_dict.SetIntKey("index", key_index);
      keysets.Append(std::move(keyset_dict));
    }
  }
  dv.SetKey("keysets", std::move(keysets));
  dv.SetBoolKey("mounted", mount_->IsMounted());
  std::string obfuscated_owner;
  homedirs_->GetOwner(&obfuscated_owner);
  dv.SetStringKey("owner", obfuscated_owner);
  dv.SetBoolKey("enterprise", homedirs_->enterprise_owned());

  dv.SetStringKey("type", mount_->GetMountTypeString());

  return dv;
}

void UserSession::PrepareWebAuthnSecret(const brillo::SecureBlob& fek,
                                        const brillo::SecureBlob& fnek) {
  // This WebAuthn secret can be rederived upon in-session user auth success
  // since they will unlock the vault keyset.
  const std::string message(kWebAuthnSecretHmacMessage);

  webauthn_secret_ = std::make_unique<brillo::SecureBlob>(
      HmacSha256(brillo::SecureBlob::Combine(fnek, fek),
                 brillo::Blob(message.cbegin(), message.cend())));
  webauthn_secret_hash_ = Sha256(*webauthn_secret_);

  // TODO(b/184393647): Since GetWebAuthnSecret is not currently used yet,
  // minimize the timer for clearing WebAuthn secret for security. Set the timer
  // to appropriate duration after secret enforcement.
  clear_webauthn_secret_timer_.Start(
      FROM_HERE, base::TimeDelta::FromSeconds(0),
      base::BindOnce(&UserSession::ClearWebAuthnSecret,
                     base::Unretained(this)));
}

void UserSession::ClearWebAuthnSecret() {
  webauthn_secret_.reset();
}

std::unique_ptr<brillo::SecureBlob> UserSession::GetWebAuthnSecret() {
  return std::move(webauthn_secret_);
}

const brillo::SecureBlob& UserSession::GetWebAuthnSecretHash() const {
  return webauthn_secret_hash_;
}

void UserSession::PrepareHibernateSecret(const brillo::SecureBlob& fek,
                                         const brillo::SecureBlob& fnek) {
  // This hibernate secret can be rederived upon in-session user auth success
  // since they will unlock the vault keyset.
  const std::string message(kHibernateSecretHmacMessage);

  hibernate_secret_ = std::make_unique<brillo::SecureBlob>(
      HmacSha256(brillo::SecureBlob::Combine(fnek, fek),
                 brillo::Blob(message.cbegin(), message.cend())));

  clear_hibernate_secret_timer_.Start(
      FROM_HERE, base::TimeDelta::FromSeconds(600),
      base::BindOnce(&UserSession::ClearHibernateSecret,
                     base::Unretained(this)));
}

void UserSession::ClearHibernateSecret() {
  hibernate_secret_.reset();
}

std::unique_ptr<brillo::SecureBlob> UserSession::GetHibernateSecret() {
  return std::move(hibernate_secret_);
}

bool UserSession::SetCredentials(const Credentials& credentials) {
  obfuscated_username_ = credentials.GetObfuscatedUsername(system_salt_);
  username_ = credentials.username();
  key_data_ = credentials.key_data();

  credential_verifier_.reset(new ScryptVerifier());
  return credential_verifier_->Set(credentials.passkey());
}

void UserSession::SetCredentials(AuthSession* auth_session) {
  username_ = auth_session->username();
  obfuscated_username_ = SanitizeUserName(username_);
  key_data_ = auth_session->current_key_data();
  credential_verifier_ = auth_session->TakeCredentialVerifier();
}

bool UserSession::VerifyUser(const std::string& obfuscated_username) const {
  return obfuscated_username_ == obfuscated_username;
}

// TODO(betuls): Move credential verification to AuthBlocks once AuthBlock
// refactor is completed.
bool UserSession::VerifyCredentials(const Credentials& credentials) const {
  ReportTimerStart(kSessionUnlockTimer);

  if (!credential_verifier_) {
    LOG(ERROR) << "Attempt to verify credentials with no verifier set";
    return false;
  }
  if (!VerifyUser(credentials.GetObfuscatedUsername(system_salt_))) {
    return false;
  }
  // If the incoming credentials have no label, then just
  // test the secret.  If it is labeled, then the label must
  // match.
  if (!credentials.key_data().label().empty() &&
      credentials.key_data().label() != key_data_.label()) {
    return false;
  }

  bool status = credential_verifier_->Verify(credentials.passkey());

  ReportTimerStop(kSessionUnlockTimer);

  return status;
}

}  // namespace cryptohome
