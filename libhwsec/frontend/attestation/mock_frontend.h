// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_FRONTEND_ATTESTATION_MOCK_FRONTEND_H_
#define LIBHWSEC_FRONTEND_ATTESTATION_MOCK_FRONTEND_H_

#include <string>

#include <attestation/proto_bindings/attestation_ca.pb.h>
#include <attestation/proto_bindings/database.pb.h>
#include <brillo/secure_blob.h>
#include <gmock/gmock.h>

#include "libhwsec/frontend/attestation/frontend.h"
#include "libhwsec/frontend/mock_frontend.h"
#include "libhwsec/status.h"

namespace hwsec {

class MockAttestationFrontend : public MockFrontend,
                                public AttestationFrontend {
 public:
  MockAttestationFrontend() = default;
  ~MockAttestationFrontend() override = default;
  MOCK_METHOD(StatusOr<brillo::SecureBlob>,
              Unseal,
              (const brillo::Blob& sealed_data),
              (const override));
  MOCK_METHOD(StatusOr<brillo::Blob>,
              Seal,
              (const brillo::SecureBlob& unsealed_data),
              (const override));
  MOCK_METHOD(StatusOr<attestation::Quote>,
              Quote,
              (DeviceConfig device_config, const brillo::Blob& key_blob),
              (const override));
  MOCK_METHOD(StatusOr<bool>,
              IsQuoted,
              (DeviceConfig device_config, const attestation::Quote& quote),
              (const override));
  MOCK_METHOD(StatusOr<DeviceConfigSettings::BootModeSetting::Mode>,
              GetCurrentBootMode,
              (),
              (const override));
  MOCK_METHOD(StatusOr<attestation::Quote>,
              CertifyNV,
              (RoSpace space, const brillo::Blob& key_blob),
              (const override));
  MOCK_METHOD(StatusOr<attestation::CertifiedKey>,
              CreateCertifiedKey,
              (const brillo::Blob& identity_key_blob,
               attestation::KeyType key_type,
               attestation::KeyUsage key_usage,
               KeyRestriction restriction,
               EndorsementAuth endorsement_auth,
               const std::string& external_data),
              (const override));
};

}  // namespace hwsec

#endif  // LIBHWSEC_FRONTEND_ATTESTATION_MOCK_FRONTEND_H_
