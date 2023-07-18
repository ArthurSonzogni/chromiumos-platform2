// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_FRONTEND_ATTESTATION_FRONTEND_H_
#define LIBHWSEC_FRONTEND_ATTESTATION_FRONTEND_H_

#include <string>

#include <attestation/proto_bindings/attestation_ca.pb.h>
#include <attestation/proto_bindings/database.pb.h>
#include <attestation/proto_bindings/keystore.pb.h>
#include <brillo/secure_blob.h>

#include "libhwsec/backend/attestation.h"
#include "libhwsec/frontend/frontend.h"
#include "libhwsec/status.h"
#include "libhwsec/structures/key.h"
#include "libhwsec/structures/operation_policy.h"
#include "libhwsec/structures/space.h"

namespace hwsec {

class AttestationFrontend : public Frontend {
 public:
  using CreateIdentityResult = Attestation::CreateIdentityResult;
  ~AttestationFrontend() override = default;
  virtual StatusOr<brillo::SecureBlob> Unseal(
      const brillo::Blob& sealed_data) const = 0;
  virtual StatusOr<brillo::Blob> Seal(
      const brillo::SecureBlob& unsealed_data) const = 0;
  virtual StatusOr<attestation::Quote> Quote(
      DeviceConfig device_config, const brillo::Blob& key_blob) const = 0;
  virtual StatusOr<bool> IsQuoted(DeviceConfig device_config,
                                  const attestation::Quote& quote) const = 0;
  virtual StatusOr<DeviceConfigSettings::BootModeSetting::Mode>
  GetCurrentBootMode() const = 0;
  virtual StatusOr<attestation::Quote> CertifyNV(
      RoSpace space, const brillo::Blob& key_blob) const = 0;
  virtual StatusOr<attestation::Quote> CertifyNVWithSize(
      RoSpace space, const brillo::Blob& key_blob, int size) const = 0;
  virtual StatusOr<attestation::CertifiedKey> CreateCertifiedKey(
      const brillo::Blob& identity_key_blob,
      attestation::KeyType key_type,
      attestation::KeyUsage key_usage,
      KeyRestriction restriction,
      EndorsementAuth endorsement_auth,
      const std::string& external_data) const = 0;
  virtual StatusOr<Attestation::CreateIdentityResult> CreateIdentity(
      attestation::KeyType key_type) const = 0;
};

}  // namespace hwsec

#endif  // LIBHWSEC_FRONTEND_ATTESTATION_FRONTEND_H_
