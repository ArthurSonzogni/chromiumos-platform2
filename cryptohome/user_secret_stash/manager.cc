// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/user_secret_stash/manager.h"

#include <string>
#include <utility>

#include <brillo/secure_blob.h>
#include <cryptohome/proto_bindings/UserDataAuth.pb.h>
#include <libhwsec-foundation/status/status_chain_macros.h>

#include "cryptohome/error/action.h"
#include "cryptohome/error/cryptohome_error.h"
#include "cryptohome/error/location_utils.h"
#include "cryptohome/error/locations.h"
#include "cryptohome/user_secret_stash/decrypted.h"
#include "cryptohome/user_secret_stash/encrypted.h"
#include "cryptohome/user_secret_stash/storage.h"
#include "cryptohome/username.h"

namespace cryptohome {
namespace {

using ::cryptohome::error::CryptohomeError;
using ::cryptohome::error::ErrorActionSet;
using ::cryptohome::error::PossibleAction;
using ::hwsec_foundation::status::MakeStatus;
using ::hwsec_foundation::status::OkStatus;

}  // namespace

UssManager::DecryptToken::DecryptToken(DecryptToken&& token)
    : uss_manager_(token.uss_manager_), username_(std::move(token.username_)) {
  token.username_->clear();
}

UssManager::DecryptToken& UssManager::DecryptToken::operator=(
    DecryptToken&& token) {
  // If this assignment is erasing a non-blank token we need to treat it like
  // destruction and decrement the token count.
  if (!this->username_->empty()) {
    DecrementTokenCount();
  }
  // Now replace the values in |this| and clear the moved-from token.
  this->uss_manager_ = token.uss_manager_;
  this->username_ = std::move(token.username_);
  token.username_->clear();
  return *this;
}

UssManager::DecryptToken::~DecryptToken() {
  if (!username_->empty()) {
    DecrementTokenCount();
  }
}

UssManager::DecryptToken::DecryptToken(UssManager* uss_manager,
                                       ObfuscatedUsername username)
    : uss_manager_(uss_manager), username_(std::move(username)) {
  IncrementTokenCount();
}

void UssManager::DecryptToken::IncrementTokenCount() {
  // This lookup should NEVER fail.
  auto decrypt_iter = uss_manager_->map_of_decrypted_.find(username_);
  CHECK(decrypt_iter != uss_manager_->map_of_decrypted_.end());
  decrypt_iter->second.token_count += 1;
}

void UssManager::DecryptToken::DecrementTokenCount() {
  // This lookup should NEVER fail.
  auto decrypt_iter = uss_manager_->map_of_decrypted_.find(username_);
  CHECK(decrypt_iter != uss_manager_->map_of_decrypted_.end());
  decrypt_iter->second.token_count -= 1;
  // If the token count falls to zero, remove the DecryptedUss from the map
  // move its EncryptedUss into the map-of-encrypted.
  if (decrypt_iter->second.token_count == 0) {
    EncryptedUss encrypted = std::move(decrypt_iter->second.uss).encrypted();
    uss_manager_->map_of_decrypted_.erase(decrypt_iter);
    auto [unused_iter, was_inserted] = uss_manager_->map_of_encrypted_.emplace(
        username_, std::move(encrypted));
    CHECK(was_inserted);  // There should never have been an existing entry.
  }
}

UssManager::UssManager(UssStorage& storage) : storage_(&storage) {}

CryptohomeStatusOr<const EncryptedUss*> UssManager::LoadEncrypted(
    const ObfuscatedUsername& username) {
  // Check to see if there's a decrypted version of this USS already loaded. If
  // there is then get the encrypted USS from there.
  auto decrypt_iter = map_of_decrypted_.find(username);
  if (decrypt_iter != map_of_decrypted_.end()) {
    return &decrypt_iter->second.uss.encrypted();
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

CryptohomeStatus UssManager::DiscardEncrypted(
    const ObfuscatedUsername& username) {
  // If the user has a decrypted USS we cannot discard it, there are live
  // references to it.
  if (map_of_decrypted_.contains(username)) {
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocUssManagerDiscardEncryptedCannotDiscardBusy),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState,
                        PossibleAction::kReboot}),
        user_data_auth::CRYPTOHOME_ERROR_REMOVE_FAILED);
  }
  // Unconditionally remove any entry from the encrypted map. There's no need to
  // check if an entry already exists, a no-op is still success.
  map_of_encrypted_.erase(username);
  return OkStatus<CryptohomeError>();
}

CryptohomeStatus UssManager::DiscardAllEncrypted() {
  // If the user has any decrypted USS data we cannot discard everything because
  // there are still live users.
  if (!map_of_decrypted_.empty()) {
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocUssManagerDiscardAllEncryptedCannotDiscardBusy),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState,
                        PossibleAction::kReboot}),
        user_data_auth::CRYPTOHOME_ERROR_REMOVE_FAILED);
  }
  // Just clear everything.
  map_of_encrypted_.clear();
  return OkStatus<CryptohomeError>();
}

CryptohomeStatusOr<UssManager::DecryptToken> UssManager::AddDecrypted(
    const ObfuscatedUsername& username, DecryptedUss decrypted_uss) {
  // If there's already an encrypted USS loaded for this user, fail.
  if (map_of_encrypted_.contains(username)) {
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocUssManagerAddDecryptedWhenEncryptedExists),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
  }
  // If there's already a decrypted USS loaded for this user, fail.
  auto decrypt_iter = map_of_decrypted_.lower_bound(username);
  if (decrypt_iter != map_of_decrypted_.end() &&
      decrypt_iter->first == username) {
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocUssManagerAddDecryptedWhenDecryptedExists),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
  }
  // If we get here then we can safely insert the new DecryptedUss without
  // collisions. Do that and return a token for accessing it.
  map_of_decrypted_.emplace_hint(
      decrypt_iter, username,
      DecryptedWithCount{.uss = std::move(decrypted_uss), .token_count = 0});
  return DecryptToken(this, username);
}

CryptohomeStatusOr<UssManager::DecryptToken> UssManager::LoadDecrypted(
    const ObfuscatedUsername& username,
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
        DecryptedWithCount{
            .uss = std::move(std::get<DecryptedUss>(decrypted_or_failure)),
            .token_count = 0});
  } else {
    // We already have a decrypted USS. However, sessions should not be able to
    // access it unless they have a working wrapped key. So before we return a
    // token we should at least verify that we can unwrap the main key and
    // decrypt the payload.
    const EncryptedUss& encrypted = decrypt_iter->second.uss.encrypted();
    ASSIGN_OR_RETURN(auto main_key,
                     encrypted.UnwrapMainKey(wrapping_id, wrapping_key));
    ASSIGN_OR_RETURN(auto payload, encrypted.DecryptPayload(main_key));
    // If we get here we were able to decrypt the payload, it's okay to let the
    // caller access DecryptedUss.
  }

  // At this point decrypt_iter is either the existing entry or a newly added
  // one. We can just return a new token for accessing it.
  return DecryptToken(this, decrypt_iter->first);
}

DecryptedUss& UssManager::GetDecrypted(const DecryptToken& token) {
  auto iter = map_of_decrypted_.find(token.username_);
  CHECK(iter != map_of_decrypted_.end())
      << "Trying to look up the DecryptedUss with an invalid token";
  return iter->second.uss;
}

}  // namespace cryptohome
