// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef U2FD_U2F_COMMAND_PROCESSOR_H_
#define U2FD_U2F_COMMAND_PROCESSOR_H_

#include <vector>

#include <base/optional.h>
#include <brillo/dbus/dbus_method_response.h>

#include "u2fd/webauthn_handler.h"

namespace u2f {

// Provides an interface to process U2F commands, including the 3 main commands
// U2fGenerate, U2fSign, and U2fSignCheckOnly we used in WebAuthn. Devices with
// different TPMs have different implementations of these commands.
class U2fCommandProcessor {
 public:
  virtual ~U2fCommandProcessor() = default;

  // Create a new pair of signing key, store key-related data in |credential_id|
  // and the public key in |credential_public_key|. |rp_id_hash| must be exactly
  // 32 bytes.
  virtual MakeCredentialResponse::MakeCredentialStatus U2fGenerate(
      const std::vector<uint8_t>& rp_id_hash,
      const std::vector<uint8_t>& credential_secret,
      PresenceRequirement presence_requirement,
      bool uv_compatible,
      const brillo::Blob* auth_time_secret_hash,
      std::vector<uint8_t>* credential_id,
      std::vector<uint8_t>* credential_public_key,
      std::vector<uint8_t>* credential_key_blob) = 0;

  // Check that credential_id is valid, and if so,
  // sign |hash_to_sign| and store the signature in |signature|.
  // |rp_id_hash| must be exactly 32 bytes.
  virtual GetAssertionResponse::GetAssertionStatus U2fSign(
      const std::vector<uint8_t>& rp_id_hash,
      const std::vector<uint8_t>& hash_to_sign,
      const std::vector<uint8_t>& credential_id,
      const std::vector<uint8_t>& credential_secret,
      const std::vector<uint8_t>* credential_key_blob,
      PresenceRequirement presence_requirement,
      std::vector<uint8_t>* signature) = 0;

  // Check that credential_id is valid and tied to |rp_id_hash|.
  virtual HasCredentialsResponse::HasCredentialsStatus U2fSignCheckOnly(
      const std::vector<uint8_t>& rp_id_hash,
      const std::vector<uint8_t>& credential_id,
      const std::vector<uint8_t>& credential_secret,
      const std::vector<uint8_t>* credential_key_blob) = 0;

  // Sign data using the attestation certificate.
  virtual MakeCredentialResponse::MakeCredentialStatus G2fAttest(
      const std::vector<uint8_t>& data,
      const brillo::SecureBlob& secret,
      uint8_t format,
      std::vector<uint8_t>* signature_out) = 0;

  virtual base::Optional<std::vector<uint8_t>> GetG2fCert() = 0;
};

}  // namespace u2f

#endif  // U2FD_U2F_COMMAND_PROCESSOR_H_
