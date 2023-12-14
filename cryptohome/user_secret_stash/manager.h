// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_USER_SECRET_STASH_MANAGER_H_
#define CRYPTOHOME_USER_SECRET_STASH_MANAGER_H_

#include <map>
#include <string>

#include <brillo/secure_blob.h>

#include "cryptohome/error/cryptohome_error.h"
#include "cryptohome/user_secret_stash/decrypted.h"
#include "cryptohome/user_secret_stash/encrypted.h"
#include "cryptohome/user_secret_stash/storage.h"
#include "cryptohome/username.h"

namespace cryptohome {

// This class is used to manage a shared set of EncryptedUss and DecryptedUss
// instances, one for each user. Sharing these instances avoids problems where
// multiple different copies of a stash can live in memory and get out of sync.
class UssManager {
 public:
  // These tokens are used to provide access to the DecryptedUss for a user.
  // Only clients who have keys to decrypt a user's USS should be able to access
  // the decrypted objects, but we don't want to require such clients to have to
  // hold onto and present a copy of the necessary keys on every lookup.
  // Instead, the manager will construct and supply one of these tokens which
  // can then be used for subsequent lookups.
  class DecryptToken {
   public:
    // Construct a "blank" token. This token will not be associated with any
    // user but it can be overrwritten with a real token.
    DecryptToken() = default;

    // Tokens can be moved around but not copied.
    DecryptToken(const DecryptToken&) = delete;
    DecryptToken& operator=(const DecryptToken&) = delete;
    DecryptToken(DecryptToken&&);
    DecryptToken& operator=(DecryptToken&&);

    // If the object is not blank (i.e. it has a non-empty username) decrement
    // the token count for this user. If the token count goes to zero this will
    // also unload the DecryptedUss.
    ~DecryptToken();

   private:
    friend class UssManager;

    // Construct a token for accessing a specific user. This is private because
    // only the UssManager can construct new non-null tokens.
    DecryptToken(UssManager* uss_manager, ObfuscatedUsername username);

    // Helpers to increment decrement the token count for the current user. Will
    // CHECK-fail if the manager does not have any entry for the username. That
    // should never happen because the manager only constructs these after
    // finding or setting up an entry.
    void IncrementTokenCount();
    void DecrementTokenCount();

    // The UssManager that created this token.
    UssManager* uss_manager_;
    // The username this token is for. Will be blank on a default or moved-from
    // token.
    ObfuscatedUsername username_;
  };

  explicit UssManager(UssStorage& storage);

  UssManager(UssManager&) = delete;
  UssManager& operator=(UssManager&) = delete;

  // Returns a reference to the encrypted USS instance for a user, or not-OK if
  // no such USS can be loaded or decrypted. If the result is OK then the
  // returned pointer will never be null.
  //
  // The pointer returned on success is invalidated by any subsequent calls to
  // a Load* function.
  CryptohomeStatusOr<const EncryptedUss*> LoadEncrypted(
      const ObfuscatedUsername& username);

  // Attempt to discard the loaded encrypted data for a user. This will succeed
  // (as a no-op) if there is no loaded data for the user. It will also succeed
  // if only encrypted data has been loaded for the user. However, if decrypted
  // data has also been loaded and there are still live tokens for it then this
  // will fail and return a not-OK status.
  CryptohomeStatus DiscardEncrypted(const ObfuscatedUsername& username);

  // Attempt to discard encrypted data for all users. This is basically
  // DiscardEncrypted for all users. It will succeed only if there is no
  // decrypted data.
  CryptohomeStatus DiscardAllEncrypted();

  // Attempt to add a new decrypted USS instance for a user. This will fail if
  // an encrypted or decrypted USS for this user already exists, and return a
  // not-OK status. Otherwise it will return a token that can be used to access
  // the newly added instance.
  CryptohomeStatusOr<DecryptToken> AddDecrypted(
      const ObfuscatedUsername& username, DecryptedUss decrypted_uss);

  // Returns a reference to the decrypted USS instance for a user, or not-OK if
  // no such USS can be loaded or decrypted. If the result is OK then the
  // returned pointer will never be null.
  CryptohomeStatusOr<DecryptToken> LoadDecrypted(
      const ObfuscatedUsername& username,
      const std::string& wrapping_id,
      const brillo::SecureBlob& wrapping_key);

  // Returns a reference to the decrypted USS instance for the user that the
  // given token provides access to.
  //
  // This will CHECK-fail if called with a blank or moved-from token.
  DecryptedUss& GetDecrypted(const DecryptToken& token);

 private:
  // The underlying storage to use for all USS access.
  UssStorage* const storage_;

  // A copy of all of the loaded encrypted USS instances.
  std::map<ObfuscatedUsername, EncryptedUss> map_of_encrypted_;

  // A copy of all of the loaded decrypted USS instances along with a token
  // count. The token count basically acts as a reference count; when the number
  // of outstanding tokens for a DecryptedUss it will be removed from this map
  // and downgraded back to an EncryptedUss.
  //
  // This is useful for two reasons. First, it avoids keeping copies of the
  // decrypted data in-memory if there are no active sessiosn that might need
  // it. Second, it provides a mechanism to remove the USS entirely (e.g. when
  // deleting a user) by terminating all active sessions and then using
  // DiscardEncrypted to flush the loaded USS from memory entirely.
  struct DecryptedWithCount {
    DecryptedUss uss;
    int token_count;
  };
  std::map<ObfuscatedUsername, DecryptedWithCount> map_of_decrypted_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_USER_SECRET_STASH_MANAGER_H_
