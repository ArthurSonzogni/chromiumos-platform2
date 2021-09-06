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

#include "cryptohome/credentials.h"
#include "cryptohome/crypto/hmac.h"
#include "cryptohome/filesystem_layout.h"
#include "cryptohome/keyset_management.h"
#include "cryptohome/storage/mount.h"

using brillo::cryptohome::home::SanitizeUserName;
using brillo::cryptohome::home::SanitizeUserNameWithSalt;

namespace cryptohome {

// Message to use when generating a secret for WebAuthn.
constexpr char kWebAuthnSecretHmacMessage[] = "AuthTimeWebAuthnSecret";

UserSession::UserSession() {}
UserSession::~UserSession() {}
UserSession::UserSession(HomeDirs* homedirs,
                         KeysetManagement* keyset_management,
                         const brillo::SecureBlob& salt,
                         const scoped_refptr<Mount> mount)
    : homedirs_(homedirs),
      keyset_management_(keyset_management),
      system_salt_(salt),
      mount_(mount) {}

MountError UserSession::MountVault(const Credentials& credentials,
                                   const Mount::MountArgs& mount_args) {
  const std::string obfuscated_username =
      credentials.GetObfuscatedUsername(system_salt_);
  bool created = false;

  // TODO(chromium:1140868, dlunev): once re-recreation logic is removed, this
  // can be moved to the service level.
  MountError error = MOUNT_ERROR_NONE;
  if (!homedirs_->CryptohomeExists(obfuscated_username, &error)) {
    if (error != MOUNT_ERROR_NONE) {
      LOG(ERROR) << "Failed to check cryptohome existence for : "
                 << obfuscated_username << " error = " << error;
      return error;
    }
    if (!mount_args.create_if_missing) {
      LOG(ERROR) << "Asked to mount nonexistent user";
      return MOUNT_ERROR_USER_DOES_NOT_EXIST;
    }

    if (!homedirs_->Create(credentials.username()) ||
        !keyset_management_->AddInitialKeyset(credentials)) {
      LOG(ERROR) << "Error creating cryptohome.";
      return MOUNT_ERROR_CREATE_CRYPTOHOME_FAILED;
    }
    keyset_management_->UpdateActivityTimestamp(obfuscated_username,
                                                kInitialKeysetIndex, 0);
    created = true;
  }

  // Verifies user's credentials and retrieves the user's file system encryption
  // keys.
  MountError code = MOUNT_ERROR_NONE;
  std::unique_ptr<VaultKeyset> vk =
      keyset_management_->LoadUnwrappedKeyset(credentials, &code);
  if (code != MOUNT_ERROR_NONE) {
    return code;
  }
  if (!vk) {
    return MOUNT_ERROR_FATAL;
  }
  FileSystemKeyset fs_keyset(*vk);

  if (!mount_->MountCryptohome(credentials.username(), fs_keyset, mount_args,
                               created, &code)) {
    // In the weird case where MountCryptohome returns false with ERROR_NONE
    // code report it as FATAL.
    return code == MOUNT_ERROR_NONE ? MOUNT_ERROR_FATAL : code;
  }
  SetCredentials(credentials, vk->GetLegacyIndex());
  UpdateActivityTimestamp(0);

  PrepareWebAuthnSecret(fs_keyset.Key().fek, fs_keyset.Key().fnek);

  return code;
}

MountError UserSession::MountVault(AuthSession* auth_session,
                                   const Mount::MountArgs& mount_args) {
  // Cannot proceed with mount if the AuthSession is not authenticated yet.
  if (auth_session->GetStatus() != AuthStatus::kAuthStatusAuthenticated) {
    return MOUNT_ERROR_FATAL;
  }
  const std::string obfuscated_username =
      SanitizeUserName(auth_session->username());
  // If the AuthSession is authenticated and the user did not exist when
  // AuthSession was started, then that means the user was created.
  bool created = !auth_session->user_exists();

  MountError code = MOUNT_ERROR_NONE;
  const FileSystemKeyset fs_keyset = auth_session->file_system_keyset();

  if (!mount_->MountCryptohome(auth_session->username(), fs_keyset, mount_args,
                               created, &code)) {
    // In the weird case where MountCryptohome returns false with ERROR_NONE
    // code report it as FATAL.
    return code == MOUNT_ERROR_NONE ? MOUNT_ERROR_FATAL : code;
  }
  // Set credentials for verification using AuthSession.
  SetCredentials(auth_session);
  UpdateActivityTimestamp(0);

  PrepareWebAuthnSecret(fs_keyset.Key().fek, fs_keyset.Key().fnek);

  return code;
}

MountError UserSession::MountEphemeral(const Credentials& credentials) {
  MountError code = mount_->MountEphemeralCryptohome(credentials.username());
  if (code == MOUNT_ERROR_NONE) {
    SetCredentials(credentials, -1);
  }
  return code;
}

MountError UserSession::MountGuest() {
  bool status = mount_->MountGuestCryptohome();
  return status ? MOUNT_ERROR_NONE : MOUNT_ERROR_FATAL;
}

bool UserSession::Unmount() {
  UpdateActivityTimestamp(0);
  return mount_->UnmountCryptohome();
}

bool UserSession::UpdateActivityTimestamp(int time_shift_sec) {
  if (!mount_->IsNonEphemeralMounted()) {
    return false;
  }
  return keyset_management_->UpdateActivityTimestamp(
      obfuscated_username_, key_index_, time_shift_sec);
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
        keyset_dict.SetIntKey("last_activity",
                              keyset->GetLastActivityTimestamp());
        if (keyset->HasKeyData()) {
          keyset_dict.SetStringKey("label", keyset->GetKeyData().label());
        }
      } else {
        keyset_dict.SetBoolKey("ok", false);
      }
      // TODO(wad) Replace key_index use with key_label() use once
      //           legacy keydata is populated.
      if (!mount_->IsEphemeral() && key_index == key_index_)
        keyset_dict.SetBoolKey("current", true);
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
  clear_webauthn_secret_timer_.Start(
      FROM_HERE, base::TimeDelta::FromSeconds(30),
      base::BindOnce(&UserSession::ClearWebAuthnSecret,
                     base::Unretained(this)));
}

void UserSession::ClearWebAuthnSecret() {
  webauthn_secret_.reset();
}

std::unique_ptr<brillo::SecureBlob> UserSession::GetWebAuthnSecret() {
  return std::move(webauthn_secret_);
}

bool UserSession::SetCredentials(const Credentials& credentials,
                                 int key_index) {
  obfuscated_username_ = credentials.GetObfuscatedUsername(system_salt_);
  username_ = credentials.username();
  key_data_ = credentials.key_data();
  key_index_ = key_index;

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
