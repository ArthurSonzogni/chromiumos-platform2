// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_CHALLENGE_CREDENTIALS_CHALLENGE_CREDENTIALS_HELPER_H_
#define CRYPTOHOME_CHALLENGE_CREDENTIALS_CHALLENGE_CREDENTIALS_HELPER_H_

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <base/callback.h>
#include <brillo/secure_blob.h>

#include "cryptohome/key_challenge_service.h"
#include "cryptohome/signature_sealing/structures.h"

namespace cryptohome {

// This class provides generation of credentials for challenge-protected vault
// keysets, and verification of key validity for such keysets.
//
// It's expected that the consumer code instantiates a single instance during
// the whole daemon lifetime. This allows to keep the resource usage
// constrained, e.g., to have a limited number of active TPM sessions.
//
// NOTE: This object supports only one operation (GenerateNew() / Decrypt() /
// VerifyKey()) at a time. Starting a new operation before the previous one
// completes will lead to cancellation of the previous operation (i.e., the
// old operation will complete with a failure).
//
// This class must be used on a single thread only.
class ChallengeCredentialsHelper {
 public:
  // This callback reports result of a GenerateNew() call.
  //
  // If the operation succeeds, |passkey| can be used for decryption of the
  // user's vault keyset, and |signature_challenge_info| containing the data to
  // be stored in the auth block state.
  // If the operation fails, the arguments will be null.
  using GenerateNewCallback =
      base::OnceCallback<void(std::unique_ptr<structure::SignatureChallengeInfo>
                                  signature_challenge_info,
                              std::unique_ptr<brillo::SecureBlob> passkey)>;

  // This callback reports result of a Decrypt() call.
  //
  // If the operation succeeds, |passkey| can be used for decryption of the
  // user's vault keyset.
  // If the operation fails, the argument will be null.
  using DecryptCallback =
      base::OnceCallback<void(std::unique_ptr<brillo::SecureBlob> passkey)>;

  // This callback reports result of a VerifyKey() call.
  //
  // The |is_key_valid| argument will be true iff the operation succeeds and
  // the provided key is valid for decryption of the given vault keyset.
  using VerifyKeyCallback = base::OnceCallback<void(bool /* is_key_valid */)>;

  // The maximum number of attempts that will be made for a single operation
  // when it fails with a transient error.
  static constexpr int kRetryAttemptCount = 3;

  ChallengeCredentialsHelper() = default;
  ChallengeCredentialsHelper(const ChallengeCredentialsHelper&) = delete;
  ChallengeCredentialsHelper& operator=(const ChallengeCredentialsHelper&) =
      delete;
  virtual ~ChallengeCredentialsHelper() = default;

  // Generates and returns fresh random-based credentials for the given user
  // and the referenced key, and also returns the encrypted
  // (challenge-protected) representation of the created secrets that should
  // be stored in the created vault keyset. This operation may involve making
  // challenge request(s) against the specified key.
  //
  // |default_pcr_map| and |extended_pcr_map| are the PCR values maps; the
  // created credentials will be protected in a way that decrypting them back is
  // possible iff at least one of these maps is satisfied.
  //
  // The result is reported via |callback|.
  virtual void GenerateNew(
      const std::string& account_id,
      const structure::ChallengePublicKeyInfo& public_key_info,
      const std::map<uint32_t, brillo::Blob>& default_pcr_map,
      const std::map<uint32_t, brillo::Blob>& extended_pcr_map,
      std::unique_ptr<KeyChallengeService> key_challenge_service,
      GenerateNewCallback callback) = 0;

  // Builds credentials for the given user, based on the encrypted
  // (challenge-protected) representation of the previously created secrets. The
  // referred cryptographic key should be the same as the one used for the
  // secrets generation via GenerateNew(); although a difference in the key's
  // supported algorithms may be tolerated in some cases. This operation
  // involves making challenge request(s) against the key.
  //
  // |keyset_challenge_info| is the encrypted representation of secrets as
  // created via GenerateNew().
  // The result is reported via |callback|.
  virtual void Decrypt(
      const std::string& account_id,
      const structure::ChallengePublicKeyInfo& public_key_info,
      const structure::SignatureChallengeInfo& keyset_challenge_info,
      bool locked_to_single_user,
      std::unique_ptr<KeyChallengeService> key_challenge_service,
      DecryptCallback callback) = 0;

  // Verifies that the specified cryptographic key is available and can be used
  // for authentication. This operation involves making challenge request(s)
  // against the key. This method is intended as a lightweight analog of
  // Decrypt() for cases where the actual credentials aren't needed.
  //
  // The result is reported via |callback|.
  virtual void VerifyKey(
      const std::string& account_id,
      const structure::ChallengePublicKeyInfo& public_key_info,
      std::unique_ptr<KeyChallengeService> key_challenge_service,
      VerifyKeyCallback callback) = 0;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_CHALLENGE_CREDENTIALS_CHALLENGE_CREDENTIALS_HELPER_H_
