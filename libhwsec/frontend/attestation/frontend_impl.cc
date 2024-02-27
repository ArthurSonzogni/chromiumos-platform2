// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libhwsec/frontend/attestation/frontend_impl.h"

#include <string>
#include <utility>
#include <vector>

#include <attestation/proto_bindings/attestation_ca.pb.h>
#include <attestation/proto_bindings/database.pb.h>
#include <brillo/secure_blob.h>
#include <libhwsec-foundation/status/status_chain_macros.h>

#include "libhwsec/backend/backend.h"
#include "libhwsec/middleware/middleware.h"
#include "libhwsec/status.h"
#include "libhwsec/structures/key.h"
#include "libhwsec/structures/operation_policy.h"
#include "libhwsec/structures/space.h"

using hwsec_foundation::status::MakeStatus;

namespace hwsec {

namespace {

StatusOr<KeyAlgoType> ToKeyAlgoType(attestation::KeyType key_type) {
  switch (key_type) {
    case attestation::KeyType::KEY_TYPE_RSA:
      return KeyAlgoType::kRsa;
    case attestation::KeyType::KEY_TYPE_ECC:
      return KeyAlgoType::kEcc;
  }
  return MakeStatus<TPMError>("Unsuported attestation key algorithm type",
                              TPMRetryAction::kNoRetry);
}

attestation::KeyType key_types[] = {attestation::KeyType::KEY_TYPE_RSA,
                                    attestation::KeyType::KEY_TYPE_ECC};

}  // namespace

Status AttestationFrontendImpl::WaitUntilReady() const {
  return middleware_.CallSync<&Backend::State::WaitUntilReady>();
}

StatusOr<brillo::SecureBlob> AttestationFrontendImpl::Unseal(
    const brillo::Blob& sealed_data) const {
  return middleware_.CallSync<&Backend::Sealing::Unseal>(
      OperationPolicy{
          .device_configs =
              DeviceConfigs{
                  DeviceConfig::kBootMode,
              },
      },
      sealed_data, Sealing::UnsealOptions{});
}

StatusOr<brillo::Blob> AttestationFrontendImpl::Seal(
    const brillo::SecureBlob& unsealed_data) const {
  return middleware_.CallSync<&Backend::Sealing::Seal>(
      OperationPolicySetting{
          .device_config_settings =
              DeviceConfigSettings{
                  .boot_mode =
                      DeviceConfigSettings::BootModeSetting{
                          .mode = std::nullopt,
                      },
              },
      },
      unsealed_data);
}

StatusOr<attestation::Quote> AttestationFrontendImpl::Quote(
    DeviceConfig device_config, const brillo::Blob& key_blob) const {
  ASSIGN_OR_RETURN(
      ScopedKey key,
      middleware_.CallSync<&Backend::KeyManagement::LoadKey>(
          OperationPolicy{}, key_blob,
          Backend::KeyManagement::LoadKeyOptions{.auto_reload = true}));

  return middleware_.CallSync<&Backend::Attestation::Quote>(
      DeviceConfigs{device_config}, key.GetKey());
}

StatusOr<bool> AttestationFrontendImpl::IsQuoted(
    DeviceConfig device_config, const attestation::Quote& quote) const {
  return middleware_.CallSync<&Backend::Attestation::IsQuoted>(
      DeviceConfigs{device_config}, quote);
}

StatusOr<DeviceConfigSettings::BootModeSetting::Mode>
AttestationFrontendImpl::GetCurrentBootMode() const {
  return middleware_.CallSync<&Backend::Config::GetCurrentBootMode>();
}

StatusOr<attestation::Quote> AttestationFrontendImpl::CertifyNV(
    RoSpace space, const brillo::Blob& key_blob) const {
  ASSIGN_OR_RETURN(
      ScopedKey key,
      middleware_.CallSync<&Backend::KeyManagement::LoadKey>(
          OperationPolicy{}, key_blob,
          Backend::KeyManagement::LoadKeyOptions{.auto_reload = true}));

  return middleware_.CallSync<&Backend::RoData::Certify>(space, key.GetKey());
}

StatusOr<attestation::CertifiedKey> AttestationFrontendImpl::CreateCertifiedKey(
    const brillo::Blob& identity_key_blob,
    attestation::KeyType key_type,
    attestation::KeyUsage key_usage,
    KeyRestriction restriction,
    EndorsementAuth endorsement_auth,
    const std::string& external_data) const {
  ASSIGN_OR_RETURN(
      const ScopedKey& identity_key,
      middleware_.CallSync<&Backend::KeyManagement::LoadKey>(
          OperationPolicy{}, identity_key_blob,
          Backend::KeyManagement::LoadKeyOptions{.auto_reload = true}));
  return middleware_.CallSync<&Backend::Attestation::CreateCertifiedKey>(
      identity_key.GetKey(), key_type, key_usage, restriction, endorsement_auth,
      external_data);
}

StatusOr<Attestation::CreateIdentityResult>
AttestationFrontendImpl::CreateIdentity(attestation::KeyType key_type) const {
  return middleware_.CallSync<&Backend::Attestation::CreateIdentity>(key_type);
}

StatusOr<brillo::Blob> AttestationFrontendImpl::GetEndorsementPublicKey(
    attestation::KeyType key_type) const {
  ASSIGN_OR_RETURN(KeyAlgoType key_algo, ToKeyAlgoType(key_type));
  return middleware_.CallSync<&Backend::KeyManagement::GetEndorsementPublicKey>(
      key_algo);
}

StatusOr<std::vector<attestation::KeyType>>
AttestationFrontendImpl::GetSupportedKeyTypes() const {
  ASSIGN_OR_RETURN(
      absl::flat_hash_set<KeyAlgoType> key_algoes,
      middleware_.CallSync<&Backend::KeyManagement::GetSupportedAlgo>());
  std::vector<attestation::KeyType> result;
  for (auto key_type : key_types) {
    ASSIGN_OR_RETURN(KeyAlgoType key_algo, ToKeyAlgoType(key_type));
    if (key_algoes.count(key_algo)) {
      result.push_back(key_type);
    }
  }

  return result;
}

StatusOr<brillo::Blob> AttestationFrontendImpl::Sign(
    const brillo::Blob& key_blob, const brillo::Blob& data) const {
  ASSIGN_OR_RETURN(
      const ScopedKey& key,
      middleware_.CallSync<&Backend::KeyManagement::LoadKey>(
          OperationPolicy{}, key_blob,
          Backend::KeyManagement::LoadKeyOptions{.auto_reload = true}));
  return middleware_.CallSync<&Backend::Signing::Sign>(
      key.GetKey(), data,
      SigningOptions{.ecdsa_encoding = SigningOptions::EcdsaEncoding::kDer});
}

}  // namespace hwsec
