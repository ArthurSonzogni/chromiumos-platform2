// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef U2FD_U2F_COMMAND_PROCESSOR_GSC_H_
#define U2FD_U2F_COMMAND_PROCESSOR_GSC_H_

#include <base/optional.h>
#include <brillo/dbus/dbus_method_response.h>
#include <trunks/cr50_headers/u2f.h>
#include <u2f/proto_bindings/u2f_interface.pb.h>

#include <functional>
#include <vector>

#include "u2fd/tpm_vendor_cmd.h"
#include "u2fd/u2f_command_processor.h"
#include "u2fd/user_state.h"
#include "u2fd/webauthn_handler.h"

namespace u2f {

class U2fCommandProcessorGsc : public U2fCommandProcessor {
 public:
  U2fCommandProcessorGsc(TpmVendorCommandProxy* tpm_proxy,
                         std::function<void()> request_presence);

  U2fCommandProcessorGsc(const U2fCommandProcessorGsc&) = delete;
  U2fCommandProcessorGsc& operator=(const U2fCommandProcessorGsc&) = delete;

  ~U2fCommandProcessorGsc() override {}

  // Runs a U2F_GENERATE command to create a new key handle, and stores the key
  // handle in |credential_id| and the public key in |credential_public_key|.
  // The flag in the U2F_GENERATE command is set according to
  // |presence_requirement|.
  MakeCredentialResponse::MakeCredentialStatus U2fGenerate(
      const std::vector<uint8_t>& rp_id_hash,
      const std::vector<uint8_t>& credential_secret,
      PresenceRequirement presence_requirement,
      bool uv_compatible,
      const brillo::Blob* auth_time_secret_hash,
      std::vector<uint8_t>* credential_id,
      std::vector<uint8_t>* credential_public_key) override;

  // Runs a U2F_SIGN command to check that credential_id is valid, and if so,
  // sign |hash_to_sign| and store the signature in |signature|.
  // The flag in the U2F_SIGN command is set according to
  // |presence_requirement|.
  // |rp_id_hash| must be exactly 32 bytes.
  GetAssertionResponse::GetAssertionStatus U2fSign(
      const std::vector<uint8_t>& rp_id_hash,
      const std::vector<uint8_t>& hash_to_sign,
      const std::vector<uint8_t>& credential_id,
      const std::vector<uint8_t>& credential_secret,
      PresenceRequirement presence_requirement,
      std::vector<uint8_t>* signature) override;

  // Runs a U2F_SIGN command with "check only" flag to check whether
  // |credential_id| is a key handle owned by this device tied to |rp_id_hash|.
  HasCredentialsResponse::HasCredentialsStatus U2fSignCheckOnly(
      const std::vector<uint8_t>& rp_id_hash,
      const std::vector<uint8_t>& credential_id,
      const std::vector<uint8_t>& credential_secret) override;

  // Run a U2F_ATTEST command to sign data using the cr50 individual attestation
  // certificate.
  MakeCredentialResponse::MakeCredentialStatus G2fAttest(
      const std::vector<uint8_t>& data,
      const brillo::SecureBlob& secret,
      uint8_t format,
      std::vector<uint8_t>* signature_out) override;

  base::Optional<std::vector<uint8_t>> GetG2fCert() override;

 private:
  friend class U2fCommandProcessorGscTest;

  // Inserts the hash of auth-time secret into a versioned KH to form a
  // WebAuthn credential id.
  void InsertAuthTimeSecretHashToCredentialId(
      const brillo::Blob* auth_time_secret_hash, std::vector<uint8_t>* input);

  // Removes the hash of auth-time secret into a credential id so that cr50
  // receives the original versioned KH.
  void RemoveAuthTimeSecretHashFromCredentialId(std::vector<uint8_t>* input);

  // Repeatedly sends u2f_generate request to the TPM if there's no presence.
  template <typename Response>
  MakeCredentialResponse::MakeCredentialStatus SendU2fGenerateWaitForPresence(
      struct u2f_generate_req* generate_req,
      Response* generate_resp,
      std::vector<uint8_t>* credential_id,
      std::vector<uint8_t>* credential_public_key);

  // Repeatedly sends u2f_sign request to the TPM if there's no presence.
  template <typename Request>
  GetAssertionResponse::GetAssertionStatus SendU2fSignWaitForPresence(
      Request* sign_req,
      struct u2f_sign_resp* sign_resp,
      std::vector<uint8_t>* signature);

  // Prompts the user for presence through |request_presence_| and calls |fn|
  // repeatedly until success or timeout.
  void CallAndWaitForPresence(std::function<uint32_t()> fn, uint32_t* status);

  static std::vector<uint8_t> EncodeCredentialPublicKeyInCBOR(
      const std::vector<uint8_t>& credential_public_key);

  TpmVendorCommandProxy* tpm_proxy_ = nullptr;
  std::function<void()> request_presence_;
};

}  // namespace u2f

#endif  // U2FD_U2F_COMMAND_PROCESSOR_GSC_H_
