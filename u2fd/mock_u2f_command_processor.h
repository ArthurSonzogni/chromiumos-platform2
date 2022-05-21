// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef U2FD_MOCK_U2F_COMMAND_PROCESSOR_H_
#define U2FD_MOCK_U2F_COMMAND_PROCESSOR_H_

#include <optional>
#include <vector>

#include <brillo/dbus/dbus_method_response.h>
#include <gmock/gmock.h>
#include <trunks/cr50_headers/u2f.h>

#include "u2fd/u2f_command_processor.h"

namespace u2f {

class MockU2fCommandProcessor : public U2fCommandProcessor {
 public:
  MockU2fCommandProcessor() = default;
  ~MockU2fCommandProcessor() override = default;

  MOCK_METHOD(MakeCredentialResponse::MakeCredentialStatus,
              U2fGenerate,
              (const std::vector<uint8_t>& rp_id_hash,
               const std::vector<uint8_t>& credential_secret,
               PresenceRequirement presence_requirement,
               bool uv_compatible,
               const brillo::Blob* auth_time_secret_hash,
               std::vector<uint8_t>* credential_id,
               CredentialPublicKey* credential_public_key,
               std::vector<uint8_t>* credential_key_blob),
              (override));

  MOCK_METHOD(GetAssertionResponse::GetAssertionStatus,
              U2fSign,
              (const std::vector<uint8_t>& rp_id_hash,
               const std::vector<uint8_t>& hash_to_sign,
               const std::vector<uint8_t>& credential_id,
               const std::vector<uint8_t>& credential_secret,
               const std::vector<uint8_t>* credential_key_blob,
               PresenceRequirement presence_requirement,
               std::vector<uint8_t>* signature),
              (override));

  MOCK_METHOD(HasCredentialsResponse::HasCredentialsStatus,
              U2fSignCheckOnly,
              (const std::vector<uint8_t>& rp_id_hash,
               const std::vector<uint8_t>& credential_id,
               const std::vector<uint8_t>& credential_secret,
               const std::vector<uint8_t>* credential_key_blob),
              (override));

  MOCK_METHOD(MakeCredentialResponse::MakeCredentialStatus,
              G2fAttest,
              (const std::vector<uint8_t>& data,
               const brillo::SecureBlob& secret,
               uint8_t format,
               std::vector<uint8_t>* signature_out),
              (override));

  MOCK_METHOD(std::optional<std::vector<uint8_t>>, GetG2fCert, (), (override));

  MOCK_METHOD(CoseAlgorithmIdentifier, GetAlgorithm, (), (override));
};

}  // namespace u2f

#endif  // U2FD_MOCK_U2F_COMMAND_PROCESSOR_H_
