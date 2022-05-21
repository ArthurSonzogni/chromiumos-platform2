// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef U2FD_U2F_COMMAND_PROCESSOR_GENERIC_H_
#define U2FD_U2F_COMMAND_PROCESSOR_GENERIC_H_

#include <functional>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <brillo/dbus/dbus_method_response.h>
#include <cryptohome/proto_bindings/UserDataAuth.pb.h>
#include <u2f/proto_bindings/u2f_interface.pb.h>
#include <user_data_auth-client/user_data_auth/dbus-proxies.h>

#include "u2fd/sign_manager/sign_manager.h"
#include "u2fd/u2f_command_processor.h"
#include "u2fd/user_state.h"
#include "u2fd/webauthn_handler.h"

namespace u2f {

class U2fCommandProcessorGeneric : public U2fCommandProcessor {
 public:
  U2fCommandProcessorGeneric(UserState* user_state, dbus::Bus* bus);

  U2fCommandProcessorGeneric(const U2fCommandProcessorGeneric&) = delete;
  U2fCommandProcessorGeneric& operator=(const U2fCommandProcessorGeneric&) =
      delete;

  ~U2fCommandProcessorGeneric() override {}

  // The generic U2F processor uses RSA algorithm and doesn't support the
  // fido-u2f attestation format, therefore the |credential_public_key_raw|
  // field is not used.
  MakeCredentialResponse::MakeCredentialStatus U2fGenerate(
      const std::vector<uint8_t>& rp_id_hash,
      const std::vector<uint8_t>& credential_secret,
      PresenceRequirement presence_requirement,
      bool uv_compatible,
      const brillo::Blob* auth_time_secret_hash,
      std::vector<uint8_t>* credential_id,
      CredentialPublicKey* credential_public_key,
      std::vector<uint8_t>* credential_key_blob) override;

  GetAssertionResponse::GetAssertionStatus U2fSign(
      const std::vector<uint8_t>& rp_id_hash,
      const std::vector<uint8_t>& hash_to_sign,
      const std::vector<uint8_t>& credential_id,
      const std::vector<uint8_t>& credential_secret,
      const std::vector<uint8_t>* credential_key_blob,
      PresenceRequirement presence_requirement,
      std::vector<uint8_t>* signature) override;

  HasCredentialsResponse::HasCredentialsStatus U2fSignCheckOnly(
      const std::vector<uint8_t>& rp_id_hash,
      const std::vector<uint8_t>& credential_id,
      const std::vector<uint8_t>& credential_secret,
      const std::vector<uint8_t>* credential_key_blob) override;

  // Currently doesn't support u2f/g2f mode.
  MakeCredentialResponse::MakeCredentialStatus G2fAttest(
      const std::vector<uint8_t>& data,
      const brillo::SecureBlob& secret,
      uint8_t format,
      std::vector<uint8_t>* signature_out) override {
    return MakeCredentialResponse::INTERNAL_ERROR;
  }

  // Currently doesn't support u2f/g2f mode.
  std::optional<std::vector<uint8_t>> GetG2fCert() override {
    return std::nullopt;
  }

  CoseAlgorithmIdentifier GetAlgorithm() override;

 private:
  U2fCommandProcessorGeneric(
      UserState* user_state,
      std::unique_ptr<org::chromium::UserDataAuthInterfaceProxyInterface>
          cryptohome_proxy,
      std::unique_ptr<SignManager> sign_manager);

  std::optional<brillo::SecureBlob> GetWebAuthnSecret();

  UserState* user_state_;
  std::unique_ptr<org::chromium::UserDataAuthInterfaceProxyInterface>
      cryptohome_proxy_;
  std::unique_ptr<SignManager> sign_manager_;

  friend class U2fCommandProcessorGenericTest;
};

}  // namespace u2f

#endif  // U2FD_U2F_COMMAND_PROCESSOR_GENERIC_H_
