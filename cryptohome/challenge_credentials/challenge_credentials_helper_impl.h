// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_CHALLENGE_CREDENTIALS_CHALLENGE_CREDENTIALS_HELPER_IMPL_H_
#define CRYPTOHOME_CHALLENGE_CREDENTIALS_CHALLENGE_CREDENTIALS_HELPER_IMPL_H_

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <base/callback.h>
#include <base/memory/weak_ptr.h>
#include <base/threading/thread_checker.h>
#include <brillo/secure_blob.h>
#include <libhwsec/status.h>

#include "cryptohome/challenge_credentials/challenge_credentials_helper.h"
#include "cryptohome/challenge_credentials/challenge_credentials_operation.h"
#include "cryptohome/error/cryptohome_tpm_error.h"
#include "cryptohome/key_challenge_service.h"
#include "cryptohome/tpm.h"

namespace cryptohome {

// Real implementation of ChallengeCredentialsHelper that is based on TPM and
// other cryptographic operations.
class ChallengeCredentialsHelperImpl final : public ChallengeCredentialsHelper {
 public:
  // The maximum number of attempts that will be made for a single operation
  // when it fails with a transient error.
  static constexpr int kRetryAttemptCount = 3;

  // |tpm| is a non-owned pointer that must stay valid for the whole lifetime of
  // the created object.
  explicit ChallengeCredentialsHelperImpl(Tpm* tpm);
  ChallengeCredentialsHelperImpl(const ChallengeCredentialsHelperImpl&) =
      delete;
  ChallengeCredentialsHelperImpl& operator=(
      const ChallengeCredentialsHelperImpl&) = delete;
  ~ChallengeCredentialsHelperImpl() override;

  // ChallengeCredentialsHelper:
  void GenerateNew(const std::string& account_id,
                   const structure::ChallengePublicKeyInfo& public_key_info,
                   const std::string& obfuscated_username,
                   std::unique_ptr<KeyChallengeService> key_challenge_service,
                   GenerateNewCallback callback) override;
  void Decrypt(const std::string& account_id,
               const structure::ChallengePublicKeyInfo& public_key_info,
               const structure::SignatureChallengeInfo& keyset_challenge_info,
               bool locked_to_single_user,
               std::unique_ptr<KeyChallengeService> key_challenge_service,
               DecryptCallback callback) override;
  void VerifyKey(const std::string& account_id,
                 const structure::ChallengePublicKeyInfo& public_key_info,
                 std::unique_ptr<KeyChallengeService> key_challenge_service,
                 VerifyKeyCallback callback) override;

 private:
  void StartDecryptOperation(
      const std::string& account_id,
      const structure::ChallengePublicKeyInfo& public_key_info,
      const structure::SignatureChallengeInfo& keyset_challenge_info,
      bool locked_to_single_user,
      int attempt_number,
      DecryptCallback callback);

  // Aborts the currently running operation, if any, and destroys all resources
  // associated with it.
  void CancelRunningOperation();

  // Wrapper for the completion callback of GenerateNew(). Cleans up resources
  // associated with the operation and forwards results to the original
  // callback.
  void OnGenerateNewCompleted(GenerateNewCallback original_callback,
                              TPMStatusOr<GenerateNewOrDecryptResult> result);

  // Wrapper for the completion callback of Decrypt(). Cleans up resources
  // associated with the operation and forwards results to the original
  // callback.
  void OnDecryptCompleted(
      const std::string& account_id,
      const structure::ChallengePublicKeyInfo& public_key_info,
      const structure::SignatureChallengeInfo& keyset_challenge_info,
      bool locked_to_single_user,
      int attempt_number,
      DecryptCallback original_callback,
      TPMStatusOr<GenerateNewOrDecryptResult> result);

  // Wrapper for the completion callback of VerifyKey(). Cleans up resources
  // associated with the operation and forwards results to the original
  // callback.
  void OnVerifyKeyCompleted(VerifyKeyCallback original_callback,
                            TPMStatus verify_status);

  // Non-owned.
  Tpm* const tpm_;
  // The key challenge service used for the currently running operation, if any.
  std::unique_ptr<KeyChallengeService> key_challenge_service_;
  // The state of the currently running operation, if any.
  std::unique_ptr<ChallengeCredentialsOperation> operation_;

  base::ThreadChecker thread_checker_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_CHALLENGE_CREDENTIALS_CHALLENGE_CREDENTIALS_HELPER_IMPL_H_
