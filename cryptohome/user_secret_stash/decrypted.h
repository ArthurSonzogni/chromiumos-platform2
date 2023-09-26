// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_USER_SECRET_STASH_DECRYPTED_H_
#define CRYPTOHOME_USER_SECRET_STASH_DECRYPTED_H_

#include <map>
#include <string>

#include <brillo/secure_blob.h>

#include "cryptohome/auth_factor/auth_factor_type.h"
#include "cryptohome/storage/file_system_keyset.h"
#include "cryptohome/user_secret_stash/encrypted.h"
#include "cryptohome/user_secret_stash/storage.h"

namespace cryptohome {

// This class represents a decrypted User Secret Stash (USS). It is built around
// the encrypted version of the class but it also has (and provides) access to
// the decrypted secrets contained within.
//
// The core interface of the class is read-only, and so does not provide any
// functions that allow you to directly modify the USS contents (e.g. by adding
// more wrapping keys). Modifications are instead done via a Transaction class
// in order to enforce atomicity: either your complete set of changes are
// applied or none are. Normal write operations would look something like:
//
// {
//    auto transaction = decrypted_uss.StartTransaction();
//    CryptohomeStatus result1 = transaction.InsertWrappedMainKey(id1, key1);
//    /* check result status */
//    CryptohomeStatus result2 = transaction.InsertWrappedMainKey(id2, key2);
//    /* check result status */
//    CryptohomeStatus commit_result = std::move(transaction).Commit();
//    /* if commit_result is OK, this is the point where the modifications will
//       be visible in the starting decrypted_uss object */
// }
//
// The enclosing {} around the entire transaction are not strictly necessary
// but they do help avoid accidentally using the transaction after the commit
// and provide a useful visual indicator of the scope of the transaction.
//
// Note that when an individual operation on a transaction fails, that does not
// fail the entire transaction. It just means that the individual mutation
// operation did not apply and will not show up. While abandoning the
// transaction on any failure is the most common and useful pattern you can
// chose to continue and commit modifications that did succeed.
class DecryptedUss {
 public:
  class Transaction {
   public:
    // This object is deliberately not copyable or movable. Transactions are
    // intended to be short lived and have a direct reference to an underlying
    // DecryptedUss object and so they should be created and store in a local
    // variable, not living beyond the immediately visible scope.
    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;

    // Insert or assign a new wrapped main key with the specified wrapping ID
    // and key. The wrapping key must be of kAesGcm256KeySize length.
    //
    // The difference between insert and assign is that insert considers it an
    // error if a wrapped key with the given ID already exists whereas assign
    // will unconditionally overwrite it.
    CryptohomeStatus InsertWrappedMainKey(
        std::string wrapping_id, const brillo::SecureBlob& wrapping_key);
    CryptohomeStatus AssignWrappedMainKey(
        std::string wrapping_id, const brillo::SecureBlob& wrapping_key);

    // Changes the wrapping ID for an existing key. This does not modify the key
    // itself in any way. Returns an error if either the old ID doesn't exist or
    // the new ID already does. This will also rename any reset secret labelled
    // with the same ID.
    CryptohomeStatus RenameWrappedMainKey(const std::string& old_wrapping_id,
                                          std::string new_wrapping_id);

    // Removes the wrapped key with the given ID. Returns an error if the given
    // ID does not exist. This will also remove any reset secret labelled with
    // the same ID.
    CryptohomeStatus RemoveWrappedMainKey(const std::string& wrapping_id);

    // Initialize the fingerprint rater limiter ID in USS. Returns an error if
    // the ID is already initialized.
    CryptohomeStatus InitializeFingerprintRateLimiterId(uint64_t id);

    // Insert or assign a new reset secret for a given label.
    //
    // The difference between insert and assign is that insert considers it an
    // error if a wrapped key with the given ID already exists whereas assign
    // will unconditionally overwrite it.
    CryptohomeStatus InsertResetSecret(std::string label,
                                       brillo::SecureBlob secret);
    CryptohomeStatus AssignResetSecret(std::string label,
                                       brillo::SecureBlob secret);

    // Insert a new rate limiter reset secret for a given type of factor.
    // Returns an error if the secret could not be inserted, which includes the
    // case where a secret already exists.
    CryptohomeStatus InsertRateLimiterResetSecret(
        AuthFactorType auth_factor_type, brillo::SecureBlob secret);

    // Attempt to commit the changes to the underlying DecryptedUss. On success
    // this will return OK and the underlying store will be modified; on failure
    // and error will be returned an none of the changes from the transaction
    // will have been applied. Writing the resulting changes out to storage will
    // also be considered a part of the commit sequence and the commit will only
    // succeed if the changes are able to be persisted. If the commit fails in
    // that case than both the in-memory and in-storage copies should remain
    // unmodified.
    //
    // Note that there is no equivalent "rollback" operation. To abandon a
    // transaction without committing any modifications you can simply discard
    // the Transaction object.
    CryptohomeStatus Commit() &&;

   private:
    // The DecryptedUss class needs to be able to construct Transaction objects.
    friend class DecryptedUss;

    Transaction(DecryptedUss& uss,
                EncryptedUss::Container container,
                std::map<std::string, brillo::SecureBlob> reset_secrets,
                std::map<AuthFactorType, brillo::SecureBlob>
                    rate_limiter_reset_secrets);

    DecryptedUss& uss_;

    // Starts as a copy of the original encrypted container. The unencrypted
    // portions will be modified by the transaction as they are made, but the
    // encrypted portion will only be rewritten during the Commit process.
    EncryptedUss::Container container_;
    // Copies of the original decrypted secrets with the modifications from the
    // transaction. Will be written over the originals by a successful Commit.
    std::map<std::string, brillo::SecureBlob> reset_secrets_;
    std::map<AuthFactorType, brillo::SecureBlob> rate_limiter_reset_secrets_;
  };

  // Create a new stash storing the given filesystem keyset, encrypted with the
  // given main key. Note that this will not persist the created USS to storage
  // yet, as a created USS without any wrapped keyset should only be persisted
  // after adding the first auth factor. It's fine that the in-memory USS isn't
  // consistent with the disk in this case, as if the USS doesn't eventually get
  // persisted, the user isn't created successfully so the inconsistency doesn't
  // matter.
  static CryptohomeStatusOr<DecryptedUss> CreateWithMainKey(
      UserUssStorage& storage,
      FileSystemKeyset file_system_keyset,
      brillo::SecureBlob main_key);

  // This will generate a random main key and call CreateWithMainKey.
  static CryptohomeStatusOr<DecryptedUss> CreateWithRandomMainKey(
      UserUssStorage& storage, FileSystemKeyset file_system_keyset);

  // Attempt to decrypt USS using using the main key.
  static CryptohomeStatusOr<DecryptedUss> FromStorageUsingMainKey(
      UserUssStorage& storage, brillo::SecureBlob main_key);

  // Attempt to decrypt USS using using a wrapped key.
  static CryptohomeStatusOr<DecryptedUss> FromStorageUsingWrappedKey(
      UserUssStorage& storage,
      const std::string& wrapping_id,
      const brillo::SecureBlob& wrapping_key);

  DecryptedUss(const DecryptedUss&) = delete;
  DecryptedUss& operator=(const DecryptedUss&) = delete;
  DecryptedUss(DecryptedUss&&) = default;
  DecryptedUss& operator=(DecryptedUss&&) = default;

  const EncryptedUss& encrypted() const { return encrypted_; }

  const FileSystemKeyset& file_system_keyset() const {
    return file_system_keyset_;
  }

  // Returns the reset secret associated with the given label, or null if there
  // is no such secret.
  std::optional<brillo::SecureBlob> GetResetSecret(
      const std::string& label) const;

  // Returns the rate limiter reset secret associated with the given type of
  // auth factor, or null if there is no such secret.
  std::optional<brillo::SecureBlob> GetRateLimiterResetSecret(
      AuthFactorType auth_factor_type) const;

  // Begin a transaction which can be used to modify this object.
  Transaction StartTransaction();

 private:
  // Given an EncryptedUss and a main key, attempt to decrypt it and construct
  // the DecryptedUss. New fields might be introduced to the USS container:
  // sometimes the default flatbuffer value (like empty blobs) are suitable,
  // while sometimes new fields should be initialized if they don't exist (like
  // fixed secrets). We perform the initialization routine of new fields in this
  // method, and if such routine is performed, the changes would be committed to
  // |storage|.
  static CryptohomeStatusOr<DecryptedUss> FromEncryptedUss(
      UserUssStorage& storage,
      EncryptedUss encrypted,
      brillo::SecureBlob main_key);

  DecryptedUss(
      UserUssStorage* storage,
      EncryptedUss encrypted,
      brillo::SecureBlob main_key,
      FileSystemKeyset file_system_keyset,
      std::map<std::string, brillo::SecureBlob> reset_secrets,
      std::map<AuthFactorType, brillo::SecureBlob> rate_limiter_reset_secrets);

  // The underlying storage of the decrypted USS instance.
  UserUssStorage* storage_;
  // The underlying raw data.
  EncryptedUss encrypted_;
  // The main key used to encrypt and decrypt the payload.
  brillo::SecureBlob main_key_;
  // Keys registered with the kernel to decrypt files and file names, together
  // with corresponding salts and signatures.
  FileSystemKeyset file_system_keyset_;
  // The reset secrets corresponding to each auth factor, by label.
  std::map<std::string, brillo::SecureBlob> reset_secrets_;
  // The reset secrets corresponding to each auth factor type's rate limiter.
  std::map<AuthFactorType, brillo::SecureBlob> rate_limiter_reset_secrets_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_USER_SECRET_STASH_DECRYPTED_H_
