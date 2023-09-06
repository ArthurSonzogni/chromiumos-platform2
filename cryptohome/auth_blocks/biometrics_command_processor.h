// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_AUTH_BLOCKS_BIOMETRICS_COMMAND_PROCESSOR_H_
#define CRYPTOHOME_AUTH_BLOCKS_BIOMETRICS_COMMAND_PROCESSOR_H_

#include <optional>
#include <string>

#include <base/functional/callback.h>
#include <brillo/secure_blob.h>
#include <cryptohome/proto_bindings/UserDataAuth.pb.h>

#include "cryptohome/error/cryptohome_error.h"
#include "cryptohome/username.h"

namespace cryptohome {

// BiometricsCommandProcessor is a stateless class that processes the biometrics
// commands sent from the biometrics service.
class BiometricsCommandProcessor {
 public:
  // OperationInput is the necessary input for the biometrics auth stack to
  // perform enrollment/authentication. These data can be retrieved by
  // interacting with PinWeaver.
  struct OperationInput {
    brillo::Blob nonce;
    brillo::Blob encrypted_label_seed;
    brillo::Blob iv;
  };

  // OperationOutput contains the data returned from biometrics auth stack after
  // enrollment/authentication for cryptohome to create/authenticate the
  // corresponding AuthFactor.
  struct OperationOutput {
    std::string record_id;
    brillo::SecureBlob auth_secret;
    brillo::SecureBlob auth_pin;
  };

  using OperationCallback =
      base::OnceCallback<void(CryptohomeStatusOr<OperationOutput>)>;

  virtual ~BiometricsCommandProcessor() = default;

  // Returns whether this BiometricsCommandProcessor is ready for accepting
  // commands.
  virtual bool IsReady() = 0;

  // Sets the repeating callback that will be triggered whenever biod emits an
  // EnrollScanDone event. The event will be packed into an
  // AuthEnrollmentProgress proto and a nonce (if enrollment is done).
  virtual void SetEnrollScanDoneCallback(
      base::RepeatingCallback<void(user_data_auth::AuthEnrollmentProgress)>
          on_done) = 0;

  // Sets the repeating callback that will be triggered whenever biod emits an
  // AuthScanDone event. The event will be packed into an AuthScanDone proto and
  // a nonce.
  virtual void SetAuthScanDoneCallback(
      base::RepeatingCallback<void(user_data_auth::AuthScanDone)> on_done) = 0;

  // Sets the repeating callback that will be triggered whenever the biod proxy
  // reports a session error.
  virtual void SetSessionFailedCallback(
      base::RepeatingCallback<void()> on_failure) = 0;

  // Fetches the nonce from the biometrics auth stack that will be used for
  // initiating the encrypted session between PinWeaver and it.
  virtual void GetNonce(
      base::OnceCallback<void(std::optional<brillo::Blob>)> callback) = 0;

  // Starts an enroll session in biod. |on_done| is triggered with whether the
  // enroll session is started successfully.
  virtual void StartEnrollSession(OperationInput payload,
                                  base::OnceCallback<void(bool)> on_done) = 0;

  // Starts an authenticate session in biod. |on_done| is triggered with whether
  // the authenticate session is started successfully.
  virtual void StartAuthenticateSession(
      ObfuscatedUsername obfuscated_username,
      OperationInput payload,
      base::OnceCallback<void(bool)> on_done) = 0;

  // Creates the actual biometrics credential in biod after enrollment is done.
  // Secret values of the credential is returned and packed into an
  // OperationOutput struct. If successful, |on_done| is triggered with the
  // OperationOutput; otherwise it's triggered with a CryptohomeError.
  virtual void CreateCredential(OperationCallback on_done) = 0;

  // Matches the collected biometrics image against all the user's enrolled
  // records after an auth scan is performed. Secret values of the credential is
  // returned and packed into an OperationOutput struct. If successful,
  // |on_done| is triggered with the OperationOutput; otherwise it's triggered
  // with a CryptohomeError.
  virtual void MatchCredential(OperationCallback on_done) = 0;

  // Ends the existing enroll session in biod.
  virtual void EndEnrollSession() = 0;

  // Ends the existing authenticate session in biod.
  virtual void EndAuthenticateSession() = 0;

  // DeleteCredential deletes the record specified by |record_id| in biod and
  // returns the result.
  enum class DeleteResult {
    kSuccess,
    kFailed,
    // kNotExist is separated from kFailed because we can treat the delete
    // operation as successful here and continue deleting the auth factor if the
    // record doesn't exist in biod.
    kNotExist,
  };
  virtual void DeleteCredential(
      ObfuscatedUsername obfuscated_username,
      const std::string& record_id,
      base::OnceCallback<void(DeleteResult)> on_done) = 0;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_AUTH_BLOCKS_BIOMETRICS_COMMAND_PROCESSOR_H_
