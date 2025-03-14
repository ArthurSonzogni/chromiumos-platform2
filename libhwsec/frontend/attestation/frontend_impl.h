// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_FRONTEND_ATTESTATION_FRONTEND_IMPL_H_
#define LIBHWSEC_FRONTEND_ATTESTATION_FRONTEND_IMPL_H_

#include <string>
#include <vector>

#include <attestation/proto_bindings/attestation_ca.pb.h>
#include <attestation/proto_bindings/database.pb.h>
#include <brillo/secure_blob.h>

#include "libhwsec/frontend/attestation/frontend.h"
#include "libhwsec/frontend/frontend_impl.h"
#include "libhwsec/status.h"
#include "libhwsec/structures/key.h"
#include "libhwsec/structures/operation_policy.h"
#include "libhwsec/structures/space.h"

namespace hwsec {

class AttestationFrontendImpl : public AttestationFrontend,
                                public FrontendImpl {
 public:
  using FrontendImpl::FrontendImpl;
  ~AttestationFrontendImpl() override = default;

  StatusOr<attestation::TpmVersion> GetVersion() const override;
  Status WaitUntilReady() const override;
  StatusOr<brillo::SecureBlob> Unseal(
      const brillo::Blob& sealed_data) const override;
  StatusOr<brillo::Blob> Seal(
      const brillo::SecureBlob& unsealed_data) const override;
  StatusOr<attestation::Quote> Quote(
      DeviceConfig device_config, const brillo::Blob& key_blob) const override;
  StatusOr<bool> IsQuoted(DeviceConfig device_config,
                          const attestation::Quote& quote) const override;
  StatusOr<DeviceConfigSettings::BootModeSetting::Mode> GetCurrentBootMode()
      const override;
  StatusOr<attestation::Quote> CertifyNV(
      RoSpace space, const brillo::Blob& key_blob) const override;
  StatusOr<attestation::CertifiedKey> CreateCertifiedKey(
      const brillo::Blob& identity_key_blob,
      attestation::KeyType key_type,
      attestation::KeyUsage key_usage,
      KeyRestriction restriction,
      EndorsementAuth endorsement_auth,
      const std::string& external_data) const override;
  StatusOr<Attestation::CreateIdentityResult> CreateIdentity(
      attestation::KeyType key_type) const override;
  StatusOr<brillo::Blob> GetEndorsementPublicKey(
      attestation::KeyType key_type,
      KeyManagement::PublicKeyEncoding encoding) const override;
  StatusOr<std::vector<attestation::KeyType>> GetSupportedKeyTypes()
      const override;
  StatusOr<brillo::Blob> Sign(const brillo::Blob& key_blob,
                              const brillo::Blob& data) const override;
  StatusOr<brillo::SecureBlob> ActivateIdentity(
      attestation::KeyType key_type,
      const brillo::Blob& identity_key_blob,
      const attestation::EncryptedIdentityCredential& encrypted_certificate)
      const override;
  StatusOr<brillo::Blob> GetEndorsementCert(
      attestation::KeyType key_type) const override;
  StatusOr<bool> IsReady() const override;
  Status FinalizeEnrollmentPreparation() const override;
};

}  // namespace hwsec

#endif  // LIBHWSEC_FRONTEND_ATTESTATION_FRONTEND_IMPL_H_
