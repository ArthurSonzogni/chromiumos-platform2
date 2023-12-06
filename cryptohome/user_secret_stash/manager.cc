// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/user_secret_stash/manager.h"

#include <string>
#include <utility>

#include <brillo/secure_blob.h>
#include <libhwsec-foundation/status/status_chain_macros.h>

#include "cryptohome/user_secret_stash/decrypted.h"
#include "cryptohome/user_secret_stash/encrypted.h"
#include "cryptohome/user_secret_stash/storage.h"
#include "cryptohome/username.h"

namespace cryptohome {

UssManager::DecryptToken::DecryptToken(DecryptToken&& token)
    : username_(std::move(token.username_)) {
  token.username_->clear();
}

UssManager::DecryptToken& UssManager::DecryptToken::operator=(
    DecryptToken&& token) {
  this->username_ = std::move(token.username_);
  token.username_->clear();
  return *this;
}

UssManager::DecryptToken::DecryptToken(ObfuscatedUsername username)
    : username_(std::move(username)) {}

UssManager::UssManager(UssStorage& storage) : storage_(&storage) {}

CryptohomeStatusOr<const EncryptedUss*> UssManager::LoadEncrypted(
    ObfuscatedUsername username) {
  // Check to see if there's a decrypted version of this USS already loaded. If
  // there is then get the encrypted USS from there.
  auto decrypt_iter = map_of_decrypted_.find(username);
  if (decrypt_iter != map_of_decrypted_.end()) {
    return &decrypt_iter->second.encrypted();
  }

  // There isn't a decrypted USS, but there could be an encrypted USS already
  // loaded. Look for that.
  auto encrypt_iter = map_of_encrypted_.lower_bound(username);
  if (encrypt_iter == map_of_encrypted_.end() ||
      encrypt_iter->first != username) {
    // There's no loaded USS, try to load it.
    UserUssStorage user_storage(*storage_, username);
    ASSIGN_OR_RETURN(auto encrypted_uss,
                     EncryptedUss::FromStorage(user_storage));
    // On a successful load we can move the USS into the map.
    encrypt_iter = map_of_encrypted_.emplace_hint(encrypt_iter, username,
                                                  std::move(encrypted_uss));
  }

  // At this point encrypt_iter is either the existing entry or a newly added
  // one. We can just return what it points at.
  return &encrypt_iter->second;
}

CryptohomeStatusOr<UssManager::DecryptToken> UssManager::LoadDecrypted(
    ObfuscatedUsername username,
    const std::string& wrapping_id,
    const brillo::SecureBlob& wrapping_key) {
  auto decrypt_iter = map_of_decrypted_.lower_bound(username);
  if (decrypt_iter == map_of_decrypted_.end() ||
      decrypt_iter->first != username) {
    UserUssStorage user_storage(*storage_, username);

    // There's no already-decrypted USS, so try to decrypt it. First step is to
    // try and get an encrypted USS.
    auto encrypt_iter = map_of_encrypted_.lower_bound(username);
    auto encrypted = [&]() -> CryptohomeStatusOr<EncryptedUss> {
      if (encrypt_iter != map_of_encrypted_.end() &&
          encrypt_iter->first == username) {
        return std::move(encrypt_iter->second);
      } else {
        return EncryptedUss::FromStorage(user_storage);
      }
    }();
    if (!encrypted.ok()) {
      return std::move(encrypted).err_status();
    }

    // Now we have an encrypted USS, so try to decrypt it.
    auto decrypted_or_failure = DecryptedUss::FromEncryptedUssUsingWrappedKey(
        user_storage, std::move(*encrypted), wrapping_id, wrapping_key);
    if (auto* failed =
            std::get_if<DecryptedUss::FailedDecrypt>(&decrypted_or_failure)) {
      // Even if the decrypt failed, we still have a good EncryptedUss. Either
      // we already had it in the encrypted map, in which case we should put it
      // back, or we didn't have it in which case we should add it. Then we can
      // return the error from the decrypt.
      if (encrypt_iter == map_of_encrypted_.end() ||
          encrypt_iter->first != username) {
        map_of_encrypted_.emplace_hint(encrypt_iter, username,
                                       std::move(failed->encrypted));
      } else {
        encrypt_iter->second = std::move(failed->encrypted);
      }
      return std::move(failed->status);
    }

    // If we get here, we have successfully decrypted the USS. We should insert
    // it into the map of decrypted, and we should remove the entry from the map
    // of encrypted if there was one.
    if (encrypt_iter != map_of_encrypted_.end() &&
        encrypt_iter->first == username) {
      map_of_encrypted_.erase(encrypt_iter);
    }
    decrypt_iter = map_of_decrypted_.emplace_hint(
        decrypt_iter, username,
        std::move(std::get<DecryptedUss>(decrypted_or_failure)));
  } else {
    // We already have a decrypted USS. However, sessions should not be able to
    // access it unless they have a working wrapped key. So before we return a
    // token we should at least verify that we can unwrap the main key and
    // decrypt the payload.
    const EncryptedUss& encrypted = decrypt_iter->second.encrypted();
    ASSIGN_OR_RETURN(auto main_key,
                     encrypted.UnwrapMainKey(wrapping_id, wrapping_key));
    ASSIGN_OR_RETURN(auto payload, encrypted.DecryptPayload(main_key));
    // If we get here we were able to decrypt the payload, it's okay to let the
    // caller access DecryptedUss.
  }

  // At this point decrypt_iter is either the existing entry or a newly added
  // one. We can just return a new token for accessing it.
  return DecryptToken(decrypt_iter->first);
}

DecryptedUss& UssManager::GetDecrypted(const DecryptToken& token) {
  auto iter = map_of_decrypted_.find(token.username_);
  CHECK(iter != map_of_decrypted_.end())
      << "Trying to look up the DecryptedUss with an invalid token";
  return iter->second;
}

}  // namespace cryptohome
