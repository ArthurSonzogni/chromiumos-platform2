// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_BACKEND_PINWEAVER_MANAGER_PINWEAVER_MANAGER_H_
#define LIBHWSEC_BACKEND_PINWEAVER_MANAGER_PINWEAVER_MANAGER_H_

#include <cstdint>
#include <map>
#include <optional>
#include <vector>

#include "libhwsec/backend/pinweaver.h"
#include "libhwsec/status.h"
#include "libhwsec/structures/operation_policy.h"

namespace hwsec {

// This is a pure virtual interface providing all the public methods necessary
// to work with pinweaver's credential functionality.
class PinWeaverManager {
 public:
  using AuthChannel = PinWeaver::AuthChannel;
  using DelaySchedule = std::map<uint32_t, uint32_t>;

  struct StartBiometricsAuthReply {
    brillo::Blob server_nonce;
    brillo::Blob iv;
    brillo::Blob encrypted_he_secret;
  };

  struct CheckCredentialReply {
    brillo::SecureBlob he_secret;
    brillo::SecureBlob reset_secret;
  };

  enum class ResetType : bool {
    kWrongAttempts = false,
    kWrongAttemptsAndExpirationTime = true
  };

  // The result object for those operation that will modify the hash tree.
  // If an operation return this struct, the tree should be updated to
  // |new_root|, otherwise the tree would out of sync.

  virtual ~PinWeaverManager() = default;

  // Inserts an LE credential into the system.
  //
  // The Low entropy credential is represented by |le_secret|, and the high
  // entropy and reset secrets by |he_secret| and |reset_secret| respectively.
  // The delay schedule which governs the rate at which CheckCredential()
  // attempts are allowed is provided in |delay_sched|. The expiration delay
  // which governs how long a credential expires after creation/reset is
  // provided in |expiration_delay|. Nullopt for |expiration_delay| means that
  // the credential won't expire.
  //
  // On success, returns the newly provisioned label.
  virtual StatusOr<uint64_t> InsertCredential(
      const std::vector<hwsec::OperationPolicySetting>& policies,
      const brillo::SecureBlob& le_secret,
      const brillo::SecureBlob& he_secret,
      const brillo::SecureBlob& reset_secret,
      const DelaySchedule& delay_sched,
      std::optional<uint32_t> expiration_delay) = 0;

  // Attempts authentication for a LE Credential.
  //
  // Checks whether the LE credential |le_secret| for a |label| is correct.
  // Additionally, the released high entropy credential and the reset secret
  // (if PW protocol version > 0) are returned.
  virtual StatusOr<CheckCredentialReply> CheckCredential(
      uint64_t label, const brillo::SecureBlob& le_secret) = 0;

  // Attempts reset the wrong attempt of a LE Credential. |reset_type|
  // indicates whether the expiration time should be reset (extended to
  // |expiration_delay| seconds from now) too.
  virtual Status ResetCredential(uint64_t label,
                                 const brillo::SecureBlob& reset_secret,
                                 ResetType reset_type) = 0;

  // Removes a credential at node with label |label|.
  virtual Status RemoveCredential(uint64_t label) = 0;

  // Returns the number of wrong authentication attempts done since the label
  // was reset or created. Returns error if |label| is not present in the tree
  // or the tree is corrupted.
  virtual StatusOr<uint32_t> GetWrongAuthAttempts(uint64_t label) = 0;

  // Returns the delay in seconds.
  virtual StatusOr<uint32_t> GetDelayInSeconds(uint64_t label) = 0;

  // Get the remaining time until the credential expires, in seconds. Nullopt
  // means the credential won't expire. 0 means the credential already expired.
  virtual StatusOr<std::optional<uint32_t>> GetExpirationInSeconds(
      uint64_t label) = 0;

  // Returns the delay schedule for a credential.
  virtual StatusOr<DelaySchedule> GetDelaySchedule(uint64_t label) = 0;

  // Inserts an biometrics rate-limiter into the system.
  //
  // The can be reset by the reset secret |reset_secret|.
  // The delay schedule which governs the rate at which CheckCredential()
  // attempts are allowed is provided in |delay_sched|. The expiration delay
  // which governs how long a credential expires after creation/reset is
  // provided in |expiration_delay|. Nullopt for |expiration_delay| means that
  // the credential won't expire.
  //
  // On success, returns the newly provisioned label.
  virtual StatusOr<uint64_t> InsertRateLimiter(
      AuthChannel auth_channel,
      const std::vector<hwsec::OperationPolicySetting>& policies,
      const brillo::SecureBlob& reset_secret,
      const DelaySchedule& delay_sched,
      std::optional<uint32_t> expiration_delay) = 0;

  // Starts an authentication attempt with a rate-limiter.
  //
  // The |client_nonce| is used to perform session key exchange, which is then
  // used for encrypting the |encrypted_he_secret| released on success.
  virtual StatusOr<StartBiometricsAuthReply> StartBiometricsAuth(
      AuthChannel auth_channel,
      const uint64_t label,
      const brillo::Blob& client_nonce) = 0;

  // Performs checks to ensure the SignInHashTree is in sync with the tree
  // state in the PinWeaverManagerBackend. If there is an out-of-sync situation,
  // this function also attempts to get the HashTree back in sync.
  //
  // Returns OkStatus on successful synchronization, and false on failure.
  // On failure, |is_locked_| will be set to true, to prevent further
  // operations during the class lifecycle.
  virtual Status SyncHashTree() = 0;
};

};  // namespace hwsec

#endif  // LIBHWSEC_BACKEND_PINWEAVER_MANAGER_PINWEAVER_MANAGER_H_
