// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_CHALLENGE_CREDENTIALS_CHALLENGE_CREDENTIALS_HELPER_IMPL_H_
#define CRYPTOHOME_CHALLENGE_CREDENTIALS_CHALLENGE_CREDENTIALS_HELPER_IMPL_H_

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <base/functional/callback.h>
#include <base/memory/weak_ptr.h>
#include <base/threading/thread_checker.h>
#include <brillo/secure_blob.h>
#include <libhwsec/frontend/cryptohome/frontend.h>
#include <libhwsec/status.h>

#include "cryptohome/challenge_credentials/challenge_credentials_helper.h"
#include "cryptohome/challenge_credentials/challenge_credentials_operation.h"
#include "cryptohome/error/cryptohome_tpm_error.h"
#include "cryptohome/key_challenge_service.h"

namespace cryptohome {

// Real implementation of ChallengeCredentialsHelper that is based on HWSec and
// other cryptographic operations.
class ChallengeCredentialsHelperImpl final : public ChallengeCredentialsHelper {
 public:
  // The maximum number of attempts that will be made for a single operation
  // when it fails with a transient error.
  static constexpr int kRetryAttemptCount = 3;

  // |hwsec| is a non-owned pointer that must stay valid for the whole lifetime
  // of the created object.
  explicit ChallengeCredentialsHelperImpl(hwsec::CryptohomeFrontend* hwsec);
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
      int attempt_number,
      DecryptCallback original_callback,
      TPMStatusOr<GenerateNewOrDecryptResult> result);

  // Wrapper for the completion callback of VerifyKey(). Cleans up resources
  // associated with the operation and forwards results to the original
  // callback.
  void OnVerifyKeyCompleted(VerifyKeyCallback original_callback,
                            TPMStatus verify_status);

  // This will check if the current TPM is available. If it is available an
  // OKStatus will be returned, if we fail to check or it is unvavailable an
  // error status will be returned.
  TPMStatus CheckTPMStatus();

  // This will check if the current TPM (if that's the underlying secure
  // element) is vulnerable to ROCA vulnerability. If not, an OkStatus is
  // returned. If we fail to check or if it is vulnerable, an error status will
  // be returned.
  TPMStatus CheckSrkRocaStatus();

  // A cache of the TPM IsReady result. This checks for the availability of a
  // TPM on device.
  std::optional<bool> tpm_ready_;

  // A cache of SRK ROCA check result. The result takes the test image into
  // consideration, i.e. no need to check whether we're on test image if this
  // variable is true.
  std::optional<bool> roca_vulnerable_;

  // Non-owned.
  hwsec::CryptohomeFrontend* const hwsec_;
  // The key challenge service used for the currently running operation, if any.
  std::unique_ptr<KeyChallengeService> key_challenge_service_;
  // The state of the currently running operation, if any.
  std::unique_ptr<ChallengeCredentialsOperation> operation_;

  base::ThreadChecker thread_checker_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_CHALLENGE_CREDENTIALS_CHALLENGE_CREDENTIALS_HELPER_IMPL_H_
