// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_FRONTEND_PINWEAVER_MANAGER_FRONTEND_H_
#define LIBHWSEC_FRONTEND_PINWEAVER_MANAGER_FRONTEND_H_

#include <cstdint>
#include <optional>
#include <vector>

#include <brillo/secure_blob.h>

#include "libhwsec/backend/pinweaver.h"
#include "libhwsec/backend/pinweaver_manager/pinweaver_manager.h"
#include "libhwsec/frontend/frontend.h"
#include "libhwsec/status.h"
#include "libhwsec/structures/operation_policy.h"

namespace hwsec {

class PinWeaverManagerFrontend : public Frontend {
 public:
  using CredentialTreeResult = PinWeaver::CredentialTreeResult;
  using GetLogResult = PinWeaver::GetLogResult;
  using ReplayLogOperationResult = PinWeaver::ReplayLogOperationResult;
  using DelaySchedule = PinWeaver::DelaySchedule;
  using PinWeaverEccPoint = PinWeaver::PinWeaverEccPoint;
  using CheckCredentialReply = PinWeaverManager::CheckCredentialReply;
  using StartBiometricsAuthReply = PinWeaverManager::StartBiometricsAuthReply;
  using ResetType = PinWeaverManager::ResetType;
  using AuthChannel = PinWeaver::AuthChannel;

  ~PinWeaverManagerFrontend() override = default;

  // Is the pinweaver enabled or not.
  virtual StatusOr<bool> IsEnabled() const = 0;

  // Gets the version of pinweaver.
  virtual StatusOr<uint8_t> GetVersion() const = 0;

  // Tries to insert a credential into the TPM.
  //
  // The LE credential to be added is in |le_secret|. Along with
  // it, its associated reset_secret |reset_secret| and the high entropy
  // credential it protects |he_secret| are also provided. The delay schedule
  // which determines the delay enforced between authentication attempts is
  // provided by |delay_schedule|. The credential is bound to the |policies|,
  // the check credential operation would only success when one of policy match.
  // And the credential has an expiration window of |expiration_delay|, it
  // expires after that many seconds after creation and each strong reset.
  //
  // If successful, the inserted label will be returned.
  virtual StatusOr<uint64_t> InsertCredential(
      const std::vector<OperationPolicySetting>& policies,
      const brillo::SecureBlob& le_secret,
      const brillo::SecureBlob& he_secret,
      const brillo::SecureBlob& reset_secret,
      const DelaySchedule& delay_schedule,
      std::optional<uint32_t> expiration_delay) const = 0;

  // Tries to verify/authenticate a credential.
  //
  // Checks whether the LE credential |le_secret| for a |label| is correct.
  //
  // On success, the returned object contains the released high entropy
  // credential and the reset secret.
  virtual StatusOr<CheckCredentialReply> CheckCredential(
      const uint64_t label, const brillo::SecureBlob& le_secret) const = 0;

  // Removes the credential which has label |label|.
  virtual Status RemoveCredential(const uint64_t label) const = 0;

  // Tries to reset a (potentially locked out) credential.
  //
  // The reset credential is |reset_secret| and the credential metadata is
  // in |orig_cred_metadata|. |reset_type| indicates whether the expiration
  // should be reset too.
  virtual Status ResetCredential(const uint64_t label,
                                 const brillo::SecureBlob& reset_secret,
                                 ResetType reset_type) const = 0;

  // Retrieves the number of wrong authentication attempts of a label.
  virtual StatusOr<uint32_t> GetWrongAuthAttempts(
      const uint64_t label) const = 0;

  // Retrieves the delay schedule of a label.
  virtual StatusOr<DelaySchedule> GetDelaySchedule(
      const uint64_t label) const = 0;

  // Retrieves the remaining delay (in seconds) of a label.
  virtual StatusOr<uint32_t> GetDelayInSeconds(const uint64_t label) const = 0;

  // Get the remaining time until the credential expires, in seconds. Nullopt
  // means the credential won't expire. 0 means the credential already expired.
  virtual StatusOr<std::optional<uint32_t>> GetExpirationInSeconds(
      const uint64_t label) const = 0;

  // Tries to establish the pairing secret of the |auth_channel| auth channel.
  //
  // The secret is established using ECDH key exchange, and |client_public_key|
  // is the public key that needs to be provided by the caller.
  //
  // If successful, the secret is established and the server's public key is
  // returned.
  virtual StatusOr<PinWeaverEccPoint> GeneratePk(
      AuthChannel auth_channel,
      const PinWeaverEccPoint& client_public_key) const = 0;

  // Tries to insert a rate-limiter credential into the TPM, bound to the
  // |auth_channel| auth channel.
  //
  // The associated reset_secret |reset_secret| is provided. The
  // delay schedule which determines the delay enforced between authentication
  // attempts is provided by |delay_schedule|. The credential is bound to the
  // |policies|, the check credential operation would only success when one of
  // policy match. And the credential has an expiration window of
  // |expiration_delay|, it expires after that many seconds after creation and
  // each strong reset.
  //
  // If successful, the inserted label will be returned.
  virtual StatusOr<uint64_t> InsertRateLimiter(
      AuthChannel auth_channel,
      const std::vector<OperationPolicySetting>& policies,
      const brillo::SecureBlob& reset_secret,
      const DelaySchedule& delay_schedule,
      std::optional<uint32_t> expiration_delay) const = 0;

  // Tries to start an authentication attempt with a rate-limiter bound to the
  // |auth_channel| auth channel.
  //
  // The label of the leaf node is in |label|.
  // The |client_nonce| is a nonce to perform session key exchange, used for
  // encrypting the |encrypted_he_secret| in the response.
  //
  // On success, the released high entropy credential will be returned encrypted
  // in |encrypted_he_secret|, and the IV used for encryption is in |iv|. The
  // nonce generated to perform the session key exchange is in |server_nonce|.
  virtual StatusOr<StartBiometricsAuthReply> StartBiometricsAuth(
      AuthChannel auth_channel,
      const uint64_t label,
      const brillo::Blob& client_nonce) const = 0;

  // Blocks future establishments of the pairing secrets until the server
  // restarts.
  //
  // If successful, future secret establishments are blocked.
  virtual Status BlockGeneratePk() const = 0;
};

}  // namespace hwsec

#endif  // LIBHWSEC_FRONTEND_PINWEAVER_MANAGER_FRONTEND_H_
