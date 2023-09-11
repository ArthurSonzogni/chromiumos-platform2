// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_USER_SECRET_STASH_USER_SECRET_STASH_H_
#define CRYPTOHOME_USER_SECRET_STASH_USER_SECRET_STASH_H_

#include <map>
#include <memory>
#include <optional>
#include <stdint.h>
#include <string>

#include <brillo/secure_blob.h>

#include "cryptohome/auth_factor/auth_factor_type.h"
#include "cryptohome/error/cryptohome_error.h"
#include "cryptohome/flatbuffer_schemas/user_secret_stash_container.h"
#include "cryptohome/storage/file_system_keyset.h"
#include "cryptohome/user_secret_stash/decrypted.h"
#include "cryptohome/user_secret_stash/encrypted.h"

namespace cryptohome {

// Structure defining the override state for key block with existing wrapping
// ID.
enum class OverwriteExistingKeyBlock {
  kEnabled,
  kDisabled,
};

// Returns whether the UserSecretStash experiment (using the USS instead of
// vault keysets) is enabled.
// The experiment is controlled by fetching a config file from gstatic. It
// matches the local USS version returned by
// `UserSecretStashExperimentVersion()` and the `last_invalid` version specified
// in the config file. If our version is greater, the experiment is enabled with
// `population` probability, and disabled otherwise. Whether the experiment is
// enabled can be overridden by creating the /var/lib/cryptohome/uss_enabled (to
// enable) or the /var/lib/cryptohome/uss_disabled (to disable) file. Unit tests
// can furthermore override this behavior using
// `SetUserSecretStashExperimentForTesting()`.
bool IsUserSecretStashExperimentEnabled(Platform* platform);

// Allows to toggle the experiment state in tests. Passing nullopt reverts to
// the default behavior. Returns the original contents before setting to allow
// tests to restore the original value.
std::optional<bool> SetUserSecretStashExperimentForTesting(
    std::optional<bool> enabled);

// This resets the static |uss_experiment_enabled| flag to simulate
// restarting cryptohomed process in the unittests.
void ResetUserSecretStashExperimentForTesting();

// RAII-style object that allows you to set the USS experiment flag (enabling or
// disabling it) in tests. The setting you apply will be cleared on destruction.
// You can use it both within individual tests by creating it on the stack, or
// in an entire fixture as a member variable.
class [[nodiscard]] SetUssExperimentOverride {
 public:
  explicit SetUssExperimentOverride(bool enabled) {
    original_value_ = SetUserSecretStashExperimentForTesting(enabled);
  }
  SetUssExperimentOverride(const SetUssExperimentOverride&) = delete;
  SetUssExperimentOverride& operator=(const SetUssExperimentOverride&) = delete;
  ~SetUssExperimentOverride() {
    SetUserSecretStashExperimentForTesting(original_value_);
  }

 private:
  std::optional<bool> original_value_;
};
// Helper that construct a SetUssExperimentOverride with the appropriate
// boolean. Generally more readable than manually constructing one with a
// boolean flag. Normally invoked by using one of:
//
//   auto uss = EnableUssExperiment();
//   auto no_uss = DisableUssExperiment();
//
inline SetUssExperimentOverride EnableUssExperiment() {
  return SetUssExperimentOverride(true);
}
inline SetUssExperimentOverride DisableUssExperiment() {
  return SetUssExperimentOverride(false);
}

// This wraps the UserSecretStash flatbuffer message, and is the only way that
// the UserSecretStash is accessed. Don't pass the raw flatbuffer around.
class UserSecretStash {
 public:
  // Sets up a UserSecretStash to protect a given filesystem keyset with the
  // specified main key.
  static CryptohomeStatusOr<std::unique_ptr<UserSecretStash>> CreateNew(
      FileSystemKeyset file_system_keyset, brillo::SecureBlob main_key);
  // Sets up a UserSecretStash to protect a given filesystem keyset with a
  // randomly generated main key.
  static CryptohomeStatusOr<std::unique_ptr<UserSecretStash>> CreateRandom(
      FileSystemKeyset file_system_keyset);
  // This deserializes the |flatbuffer| into a UserSecretStashContainer table.
  // Besides unencrypted data, that table contains a ciphertext, which is
  // decrypted with the |main_key| using AES-GCM-256. It doesn't return the
  // plaintext, it populates the fields of the class with the encrypted message.
  static CryptohomeStatusOr<std::unique_ptr<UserSecretStash>>
  FromEncryptedContainer(const brillo::Blob& flatbuffer,
                         const brillo::SecureBlob& main_key);
  // Same as |FromEncryptedContainer()|, but the main key is unwrapped from the
  // USS container using the given wrapping key.
  static CryptohomeStatusOr<std::unique_ptr<UserSecretStash>>
  FromEncryptedContainerWithWrappingKey(const brillo::Blob& flatbuffer,
                                        const std::string& wrapping_id,
                                        const brillo::SecureBlob& wrapping_key);

  virtual ~UserSecretStash() = default;

  // Because this class contains raw secrets, it should never be copy-able.
  UserSecretStash(const UserSecretStash&) = delete;
  UserSecretStash& operator=(const UserSecretStash&) = delete;

  const FileSystemKeyset& GetFileSystemKeyset() const;

  // This gets the reset secret for the auth factor with the associated
  // |label|.
  std::optional<brillo::SecureBlob> GetResetSecretForLabel(
      const std::string& label) const;

  // This sets the reset secret for an auth factor with the associated |label|.
  // This does not overwrite an existing reset secret. It returns if the
  // insertion succeeded.
  [[nodiscard]] bool SetResetSecretForLabel(const std::string& label,
                                            const brillo::SecureBlob& secret);

  // This removes the reset secret for an auth factor with the associated
  // |label|. Returns false if reset secret wasn't present for provided |label|,
  // true otherwise.
  // TODO(b/238897234): Move this to RemoveWrappedMainKey.
  bool RemoveResetSecretForLabel(const std::string& label);

  // This gets the reset secret for the rate limiter of the |auth_factor_type|.
  std::optional<brillo::SecureBlob> GetRateLimiterResetSecret(
      AuthFactorType auth_factor_type) const;

  // This sets the reset secret for the rate limiter of the |auth_factor_type|.
  // This does not overwrite an existing reset secret. It returns if the
  // insertion succeeded.
  [[nodiscard]] bool SetRateLimiterResetSecret(
      AuthFactorType auth_factor_type, const brillo::SecureBlob& secret);

  std::optional<uint64_t> GetFingerprintRateLimiterId();

  // Overwrite an existing id is prohibited. Returns if the initialization
  // succeeds.
  [[nodiscard]] bool InitializeFingerprintRateLimiterId(uint64_t id);

  // The OS version on which this particular user secret stash was originally
  // created. The format is the one of the CHROMEOS_RELEASE_VERSION field in
  // /etc/lsb-release, e.g.: "11012.0.2018_08_28_1422". Empty if the version
  // fetch failed at the creation time.
  // !!!WARNING!!!: This value is not authenticated nor validated. It must not
  // be used for security-critical features.
  const std::string& GetCreatedOnOsVersion() const;

  // Returns whether there's a wrapped key block with the given wrapping ID.
  bool HasWrappedMainKey(const std::string& wrapping_id) const;

  // Unwraps (decrypts) the USS main key from the wrapped key block with the
  // given wrapping ID. Returns a status if it doesn't exist or the unwrapping
  // fails.
  CryptohomeStatusOr<brillo::SecureBlob> UnwrapMainKey(
      const std::string& wrapping_id,
      const brillo::SecureBlob& wrapping_key) const;

  // Wraps (encrypts) the USS main key using the given wrapped key. The wrapped
  // data is added into the USS as a wrapped key block with the given wrapping
  // ID. Returns a status if the wrapping ID is already used and |clobber| is
  // disabled, or the wrapping fails.
  CryptohomeStatus AddWrappedMainKey(const std::string& wrapping_id,
                                     const brillo::SecureBlob& wrapping_key,
                                     OverwriteExistingKeyBlock clobber);

  // Changes the wrapping ID for an existing key. This does not modify the key
  // itself in any way. Returns false if either the old ID doesn't exist or the
  // new ID already does.
  bool RenameWrappedMainKey(const std::string& old_wrapping_id,
                            const std::string& new_wrapping_id);

  // Removes the wrapped key with the given ID. If it doesn't exist, returns
  // false.
  bool RemoveWrappedMainKey(const std::string& wrapping_id);

  // The object is converted to a UserSecretStashPayload table, serialized,
  // encrypted with AES-GCM-256, and serialized as a UserSecretStashContainer
  // table into a blob.
  CryptohomeStatusOr<brillo::Blob> GetEncryptedContainer();

  // Functions to create a snapshot of the current state of the USS and to
  // restore it to the state from a given snapshot. These are generally useful
  // when you want to be able to make changes to the state of the USS that can
  // be reverted, e.g. if persisting the changes fails.
  class Snapshot {
   public:
    // The snapshot can be moved around but not copied. In general snapshots
    // should be "use once".
    Snapshot(const Snapshot&) = delete;
    Snapshot& operator=(const Snapshot&) = delete;
    Snapshot(Snapshot&&) = default;
    Snapshot& operator=(Snapshot&&) = default;

   private:
    friend class UserSecretStash;

    explicit Snapshot(DecryptedUss decrypted);

    // This is basically a copy of the internal state of the USS.
    DecryptedUss decrypted_;
  };
  // Take will capture the current state of the USS in the returned snapshot.
  Snapshot TakeSnapshot() const;
  // Restore will restore the USS to the state it was in at the time that
  // TakeSnapshot() was called. It takes an rvalue reference because snapshots
  // are intended to be single use only and so restoring "consumes" it.
  void RestoreSnapshot(Snapshot&& snapshot);

 private:
  explicit UserSecretStash(DecryptedUss decrypted);

  // The underlying decrypted USS object.
  DecryptedUss decrypted_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_USER_SECRET_STASH_USER_SECRET_STASH_H_
