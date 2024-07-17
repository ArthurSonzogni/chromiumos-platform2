// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_blocks/cryptohome_recovery_service.h"

#include <memory>
#include <utility>

#include <brillo/secure_blob.h>
#include <libhwsec-foundation/status/status_chain.h>
#include <libhwsec/frontend/recovery_crypto/frontend.h>
#include <libstorage/platform/platform.h>

#include "cryptohome/auth_blocks/prepare_token.h"
#include "cryptohome/cryptorecovery/recovery_crypto_hsm_cbor_serialization.h"
#include "cryptohome/cryptorecovery/recovery_crypto_impl.h"
#include "cryptohome/error/action.h"
#include "cryptohome/error/cryptohome_crypto_error.h"
#include "cryptohome/error/cryptohome_error.h"
#include "cryptohome/error/locations.h"
#include "cryptohome/key_objects.h"

namespace cryptohome {
namespace {

using ::cryptohome::error::CryptohomeCryptoError;
using ::cryptohome::error::CryptohomeError;
using ::cryptohome::error::ErrorActionSet;
using ::cryptohome::error::PossibleAction;
using ::hwsec_foundation::status::MakeStatus;
using ::hwsec_foundation::status::OkStatus;

}  // namespace

CryptohomeRecoveryAuthBlockService::CryptohomeRecoveryAuthBlockService(
    libstorage::Platform* platform,
    const hwsec::RecoveryCryptoFrontend* recovery_hwsec)
    : platform_(platform), recovery_hwsec_(recovery_hwsec) {}

void CryptohomeRecoveryAuthBlockService::GenerateRecoveryRequest(
    const ObfuscatedUsername& obfuscated_username,
    const cryptorecovery::RequestMetadata& request_metadata,
    const brillo::Blob& epoch_response,
    const CryptohomeRecoveryAuthBlockState& state,
    PreparedAuthFactorToken::Consumer on_done) {
  // Wrap the on_done callback with a lambda that will log an error message on
  // failure. This will be captured by the crash reporter to generate a
  // synthetic crash report.
  on_done = base::BindOnce(
      [](PreparedAuthFactorToken::Consumer wrapped_on_done,
         CryptohomeStatusOr<std::unique_ptr<PreparedAuthFactorToken>> token) {
        if (!token.ok()) {
          // Note: the error format should match `cryptohome_recovery_failure`
          // in crash-reporter/anomaly_detector.cc
          LOG(ERROR)
              << "Cryptohome Recovery Request generation failure, error = "
              << token.err_status()->local_legacy_error().value();
        }
        std::move(wrapped_on_done).Run(std::move(token));
      },
      std::move(on_done));

  // Check if the required fields are set on CryptohomeRecoveryAuthBlockState.
  if (state.hsm_payload.empty() || state.channel_pub_key.empty() ||
      state.encrypted_channel_priv_key.empty()) {
    LOG(ERROR) << "CryptohomeRecoveryAuthBlockState is invalid";
    CryptohomeStatus status = MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(kLocAuthBlockStateInvalidInGenerateRecoveryRequest),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        CryptoError::CE_OTHER_CRYPTO);
    std::move(on_done).Run(std::move(status));
    return;
  }

  // Deserialize HSM payload from CryptohomeRecoveryAuthBlockState.
  cryptorecovery::HsmPayload hsm_payload;
  if (!cryptorecovery::DeserializeHsmPayloadFromCbor(state.hsm_payload,
                                                     &hsm_payload)) {
    LOG(ERROR) << "Failed to deserialize HSM payload";
    CryptohomeStatus status = MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(
            kLocFailedDeserializeHsmPayloadInGenerateRecoveryRequest),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        CryptoError::CE_OTHER_CRYPTO);
    std::move(on_done).Run(std::move(status));
    return;
  }

  // Parse epoch response, which is sent from Chrome, to proto.
  cryptorecovery::CryptoRecoveryEpochResponse epoch_response_proto;
  if (!epoch_response_proto.ParseFromArray(epoch_response.data(),
                                           epoch_response.size())) {
    LOG(ERROR) << "Failed to parse epoch response";
    CryptohomeStatus status = MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(
            kLocFailedParseEpochResponseInGenerateRecoveryRequest),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        CryptoError::CE_OTHER_CRYPTO);
    std::move(on_done).Run(std::move(status));
    return;
  }

  if (!recovery_hwsec_) {
    CryptohomeStatus status = MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(
            kLocFailedToGetRecoveryCryptoBackendInGenerateRecoveryRequest),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        CryptoError::CE_OTHER_CRYPTO);
    std::move(on_done).Run(std::move(status));
    return;
  }

  std::unique_ptr<cryptorecovery::RecoveryCryptoImpl> recovery =
      cryptorecovery::RecoveryCryptoImpl::Create(recovery_hwsec_, platform_);

  // Generate recovery request proto which will be sent back to Chrome, and then
  // to the recovery server.
  cryptorecovery::GenerateRecoveryRequestRequest
      generate_recovery_request_input_param{
          .hsm_payload = hsm_payload,
          .request_meta_data = request_metadata,
          .epoch_response = epoch_response_proto,
          .encrypted_rsa_priv_key = state.encrypted_rsa_priv_key,
          .encrypted_channel_priv_key = state.encrypted_channel_priv_key,
          .channel_pub_key = state.channel_pub_key,
          .obfuscated_username = obfuscated_username};
  CryptohomeRecoveryPrepareOutput prepare_output;
  if (!recovery->GenerateRecoveryRequest(generate_recovery_request_input_param,
                                         &prepare_output.recovery_rpc_request,
                                         &prepare_output.ephemeral_pub_key)) {
    LOG(ERROR) << "Call to GenerateRecoveryRequest failed";
    // TODO(b/231297066): send more specific error.
    CryptohomeStatus status = MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(kLocFailedGenerateRecoveryRequest),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        CryptoError::CE_OTHER_CRYPTO);
    std::move(on_done).Run(std::move(status));
    return;
  }

  // Construct and return the token for the completed preparation.
  auto token = std::make_unique<Token>(std::move(prepare_output));
  std::move(on_done).Run(std::move(token));
}

CryptohomeRecoveryAuthBlockService::Token::Token(
    CryptohomeRecoveryPrepareOutput output)
    : PreparedAuthFactorToken(
          AuthFactorType::kCryptohomeRecovery,
          {.cryptohome_recovery_prepare_output = std::move(output)}),
      terminate_(*this) {}

// Termination is implemented as a no-op. We have no active internal state
// associated with the request and so to terminate we simply discard the token
// and all of the values generated by the prepare operation.
CryptohomeStatus
CryptohomeRecoveryAuthBlockService::Token::TerminateAuthFactor() {
  return OkStatus<CryptohomeError>();
}

}  // namespace cryptohome
