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

  // Unseals the |sealed_data| with current boot mode.
  virtual StatusOr<brillo::SecureBlob> Unseal(
      const brillo::Blob& sealed_data) const = 0;

  // Seal the |unsealed_data| with current boot mode.
  virtual StatusOr<brillo::Blob> Seal(
      const brillo::SecureBlob& unsealed_data) const = 0;

  // Quotes the |device_configs| with |key_blob|.
  virtual StatusOr<attestation::Quote> Quote(
      DeviceConfig device_config, const brillo::Blob& key_blob) const = 0;

  // Checks if |quote| is valid for a single device config specified by
  // |device_configs|.
  virtual StatusOr<bool> IsQuoted(DeviceConfig device_config,
                                  const attestation::Quote& quote) const = 0;

  // Returns the current boot mode if it is valid.
  virtual StatusOr<DeviceConfigSettings::BootModeSetting::Mode>
  GetCurrentBootMode() const = 0;

  // Certifies data of the |space| with the |key_blob|.
  virtual StatusOr<attestation::Quote> CertifyNV(
      RoSpace space, const brillo::Blob& key_blob) const = 0;

  // Create a key with |key_type|, |key_usage|, and |restriction|, and
  // certifies it by |identity_key| with |external_data|. When
  // |endorsement_auth| is kEndorsementAuth, the key is created as a virtual
  // endorsement key (vEK).
  virtual StatusOr<attestation::CertifiedKey> CreateCertifiedKey(
      const brillo::Blob& identity_key_blob,
      attestation::KeyType key_type,
      attestation::KeyUsage key_usage,
      KeyRestriction restriction,
      EndorsementAuth endorsement_auth,
      const std::string& external_data) const = 0;

  // Creates identity of |key_type| type, which contains
  // attestation::IdentityKey and attestation::IdentityBinding.
  virtual StatusOr<Attestation::CreateIdentityResult> CreateIdentity(
      attestation::KeyType key_type) const = 0;
};

}  // namespace hwsec

#endif  // LIBHWSEC_FRONTEND_ATTESTATION_FRONTEND_H_
