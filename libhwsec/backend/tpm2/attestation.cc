// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libhwsec/backend/tpm2/attestation.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <attestation/proto_bindings/attestation_ca.pb.h>
#include <attestation/proto_bindings/database.pb.h>
#include <crypto/scoped_openssl_types.h>
#include <crypto/sha2.h>
#include <trunks/mock_tpm_utility.h>
#include <trunks/multiple_authorization_delegate.h>
#include <libhwsec-foundation/status/status_chain_macros.h>
#include <trunks/tpm_generated.h>

#include "libhwsec/backend/tpm2/static_utils.h"
#include "libhwsec/error/tpm2_error.h"
#include "libhwsec/status.h"
#include "libhwsec/structures/key.h"
#include "libhwsec/structures/operation_policy.h"

using brillo::BlobFromString;
using brillo::BlobToString;
using hwsec_foundation::status::MakeStatus;
using trunks::TPM_RC;
using trunks::TPM_RC_SUCCESS;

namespace hwsec {
namespace {

constexpr size_t kRandomCertifiedKeyPasswordLength = 32;

}  // namespace

StatusOr<attestation::Quote> AttestationTpm2::Quote(
    DeviceConfigs device_configs, Key key) {
  if (device_configs.none()) {
    return MakeStatus<TPMError>("No device config specified",
                                TPMRetryAction::kNoRetry);
  }

  attestation::Quote quote;
  ASSIGN_OR_RETURN(const KeyTpm2& key_data, key_management_.GetKeyData(key));
  ASSIGN_OR_RETURN(const ConfigTpm2::PcrMap& pcr_map,
                   config_.ToPcrMap(device_configs),
                   _.WithStatus<TPMError>("Failed to get PCR map"));

  if (pcr_map.size() == 1) {
    int pcr = pcr_map.begin()->first;
    ASSIGN_OR_RETURN(const std::string& value, config_.ReadPcr(pcr),
                     _.WithStatus<TPMError>("Failed to read PCR"));
    quote.set_quoted_pcr_value(value);
  }

  std::unique_ptr<trunks::AuthorizationDelegate> delegate =
      context_.GetTrunksFactory().GetPasswordAuthorization("");

  trunks::TPMT_SIG_SCHEME scheme;
  scheme.details.any.hash_alg = trunks::TPM_ALG_SHA256;
  ASSIGN_OR_RETURN(scheme.scheme,
                   signing_.GetSignAlgorithm(key_data, SigningOptions{}),
                   _.WithStatus<TPMError>("Failed to get signing algorithm"));

  trunks::TPML_PCR_SELECTION pcr_select;
  pcr_select.count = 1;
  ASSIGN_OR_RETURN(pcr_select.pcr_selections[0],
                   config_.ToPcrSelection(device_configs),
                   _.WithStatus<TPMError>(
                       "Failed to convert device configs to PCR selection"));

  const trunks::TPM_HANDLE& key_handle = key_data.key_handle;
  std::string key_name;
  RETURN_IF_ERROR(MakeStatus<TPM2Error>(context_.GetTpmUtility().GetKeyName(
                      key_handle, &key_name)))
      .WithStatus<TPMError>("Failed to get key name");

  trunks::TPM2B_ATTEST quoted_struct;
  trunks::TPMT_SIGNATURE signature;
  RETURN_IF_ERROR(
      MakeStatus<TPM2Error>(context_.GetTrunksFactory().GetTpm()->QuoteSync(
          key_handle, key_name,
          trunks::Make_TPM2B_DATA("") /* No qualifying data */, scheme,
          pcr_select, &quoted_struct, &signature, delegate.get())))
      .WithStatus<TPMError>("Failed to quote");

  if (device_configs[DeviceConfig::kDeviceModel]) {
    if (StatusOr<std::string> hwid = config_.GetHardwareID(); !hwid.ok()) {
      LOG(WARNING) << "Failed to get Hardware ID: " << hwid.status();
    } else {
      quote.set_pcr_source_hint(hwid.value());
    }
  }
  ASSIGN_OR_RETURN(const std::string& sig,
                   SerializeFromTpmSignature(signature));
  quote.set_quote(sig);

  if (quoted_struct.size > sizeof(quoted_struct.attestation_data)) {
    return MakeStatus<TPMError>("Quoted struct overflow",
                                TPMRetryAction::kNoRetry);
  }
  quote.set_quoted_data(StringFrom_TPM2B_ATTEST(quoted_struct));

  return quote;
}

// TODO(b/141520502): Verify the quote against expected output.
StatusOr<bool> AttestationTpm2::IsQuoted(DeviceConfigs device_configs,
                                         const attestation::Quote& quote) {
  if (device_configs.none()) {
    return MakeStatus<TPMError>("No device config specified",
                                TPMRetryAction::kNoRetry);
  }
  if (!quote.has_quoted_data()) {
    return MakeStatus<TPMError>("Invalid attestation::Quote",
                                TPMRetryAction::kNoRetry);
  }

  std::string quoted_data = quote.quoted_data();

  trunks::TPMS_ATTEST quoted_struct;
  RETURN_IF_ERROR(MakeStatus<TPM2Error>(trunks::Parse_TPMS_ATTEST(
                      &quoted_data, &quoted_struct, nullptr)))
      .WithStatus<TPMError>("Failed to parse TPMS_ATTEST");

  if (quoted_struct.magic != trunks::TPM_GENERATED_VALUE) {
    return MakeStatus<TPMError>("Bad magic value", TPMRetryAction::kNoRetry);
  }
  if (quoted_struct.type != trunks::TPM_ST_ATTEST_QUOTE) {
    return MakeStatus<TPMError>("Not a quote", TPMRetryAction::kNoRetry);
  }

  const trunks::TPML_PCR_SELECTION& pcr_select =
      quoted_struct.attested.quote.pcr_select;
  if (pcr_select.count != 1) {
    return MakeStatus<TPMError>("Wrong number of PCR selection",
                                TPMRetryAction::kNoRetry);
  }
  const trunks::TPMS_PCR_SELECTION& pcr_selection =
      pcr_select.pcr_selections[0];

  ASSIGN_OR_RETURN(trunks::TPMS_PCR_SELECTION expected_pcr_selection,
                   config_.ToPcrSelection(device_configs),
                   _.WithStatus<TPMError>(
                       "Failed to convert device configs to PCR selection"));

  if (pcr_selection.sizeof_select != expected_pcr_selection.sizeof_select) {
    return MakeStatus<TPMError>("Size of pcr_selections mismatched",
                                TPMRetryAction::kNoRetry);
  }

  for (int i = 0; i < pcr_selection.sizeof_select; ++i) {
    if (pcr_selection.pcr_select[i] != expected_pcr_selection.pcr_select[i]) {
      return false;
    }
  }
  return true;
}

StatusOr<AttestationTpm2::CertifyKeyResult> AttestationTpm2::CertifyKey(
    Key key, Key identity_key, const std::string& external_data) {
  ASSIGN_OR_RETURN(const KeyTpm2& key_data, key_management_.GetKeyData(key),
                   _.WithStatus<TPMError>("Failed to get key data"));
  ASSIGN_OR_RETURN(const KeyTpm2& identity_key_data,
                   key_management_.GetKeyData(identity_key),
                   _.WithStatus<TPMError>("Failed to get identity key data"));
  const trunks::TPM_HANDLE& key_handle = key_data.key_handle;
  const trunks::TPM_HANDLE& identity_key_handle = identity_key_data.key_handle;

  std::string key_name;
  std::string identity_key_name;
  RETURN_IF_ERROR(MakeStatus<TPM2Error>(context_.GetTpmUtility().GetKeyName(
                      key_handle, &key_name)))
      .WithStatus<TPMError>("Failed to get key name");
  RETURN_IF_ERROR(MakeStatus<TPM2Error>(context_.GetTpmUtility().GetKeyName(
                      identity_key_handle, &identity_key_name)))
      .WithStatus<TPMError>("Failed to get key name");

  trunks::TPMT_SIG_SCHEME scheme;
  scheme.details.any.hash_alg = trunks::TPM_ALG_SHA256;
  ASSIGN_OR_RETURN(
      scheme.scheme,
      signing_.GetSignAlgorithm(identity_key_data, SigningOptions{}),
      _.WithStatus<TPMError>("Failed to get signing algorithm"));

  std::string certified_key_password;
  if (key_data.cache.policy.permission.type == PermissionType::kAuthValue &&
      key_data.cache.policy.permission.auth_value.has_value()) {
    certified_key_password =
        key_data.cache.policy.permission.auth_value.value().to_string();
  }

  std::unique_ptr<trunks::AuthorizationDelegate>
      certified_key_password_authorization =
          context_.GetTrunksFactory().GetPasswordAuthorization(
              certified_key_password);
  std::unique_ptr<trunks::AuthorizationDelegate> empty_password_authorization =
      context_.GetTrunksFactory().GetPasswordAuthorization("");

  trunks::MultipleAuthorizations authorization;
  authorization.AddAuthorizationDelegate(
      certified_key_password_authorization.get());
  authorization.AddAuthorizationDelegate(empty_password_authorization.get());

  trunks::TPM2B_ATTEST certify_info;
  trunks::TPMT_SIGNATURE signature;
  RETURN_IF_ERROR(
      MakeStatus<TPM2Error>(context_.GetTrunksFactory().GetTpm()->CertifySync(
          key_handle, key_name, identity_key_handle, identity_key_name,
          trunks::Make_TPM2B_DATA(external_data), scheme, &certify_info,
          &signature, &authorization)))
      .WithStatus<TPMError>("Failed to certify key");
  ASSIGN_OR_RETURN(const std::string& sig,
                   SerializeFromTpmSignature(signature));

  return CertifyKeyResult{
      .certify_info = StringFrom_TPM2B_ATTEST(certify_info),
      .signature = sig,
  };
}

StatusOr<attestation::CertifiedKey> AttestationTpm2::CreateCertifiedKey(
    Key identity_key,
    attestation::KeyType key_type,
    attestation::KeyUsage key_usage,
    KeyRestriction restriction,
    EndorsementAuth endorsement_auth,
    const std::string& external_data) {
  KeyAlgoType key_algo;
  switch (key_type) {
    case attestation::KeyType::KEY_TYPE_RSA:
      key_algo = hwsec::KeyAlgoType::kRsa;
      break;
    case attestation::KeyType::KEY_TYPE_ECC:
      key_algo = hwsec::KeyAlgoType::kEcc;
      break;
    default:
      return MakeStatus<TPMError>("Unsuported key algorithm type",
                                  TPMRetryAction::kNoRetry);
  }

  OperationPolicySetting policy;
  // When generating endorsement key (for vEK), we would create the key with
  // both policy and a temporary random password.
  if (endorsement_auth == EndorsementAuth::kEndorsement) {
    ASSIGN_OR_RETURN(
        const brillo::SecureBlob& random_password,
        random_.RandomSecureBlob(kRandomCertifiedKeyPasswordLength),
        _.WithStatus<TPMError>("Failed to create random password"));
    policy = OperationPolicySetting{
        .device_config_settings =
            DeviceConfigSettings{
                .use_endorsement_auth = true,
            },
        .permission =
            Permission{
                .auth_value = std::move(random_password),
            },
    };
  }

  ASSIGN_OR_RETURN(
      const KeyManagement::CreateKeyResult& create_key_result,
      key_management_.CreateKey(
          policy, key_algo, KeyManagement::LoadKeyOptions{.auto_reload = true},
          KeyManagement::CreateKeyOptions{
              .allow_software_gen = false,
              .allow_decrypt =
                  key_usage == attestation::KeyUsage::KEY_USAGE_DECRYPT,
              .allow_sign = key_usage == attestation::KeyUsage::KEY_USAGE_SIGN,
              .restricted = restriction == KeyRestriction::kRestricted,
          }),
      _.WithStatus<TPMError>("Failed to create key"));
  const ScopedKey& key = create_key_result.key;
  const brillo::Blob& key_blob = create_key_result.key_blob;

  ASSIGN_OR_RETURN(const CertifyKeyResult& certify_key_result,
                   CertifyKey(key.GetKey(), identity_key, external_data),
                   _.WithStatus<TPMError>("Failed to certify key"));

  ASSIGN_OR_RETURN(const KeyTpm2& key_data,
                   key_management_.GetKeyData(key.GetKey()),
                   _.WithStatus<TPMError>("Failed to get key data"));

  const trunks::TPMT_PUBLIC& public_data = key_data.cache.public_area;
  std::string serialized_public_key;
  RETURN_IF_ERROR(MakeStatus<TPM2Error>(trunks::Serialize_TPMT_PUBLIC(
                      public_data, &serialized_public_key)))
      .WithStatus<TPMError>("Failed to serialized TPMT_PUBLIC");

  ASSIGN_OR_RETURN(
      const brillo::Blob& public_key_der,
      key_management_.GetPublicKeyDer(key.GetKey(),
                                      /*use_rsa_subject_key_info=*/false),
      _.WithStatus<TPMError>("Failed to get public key in DER format"));

  attestation::CertifiedKey certified_key;
  certified_key.set_key_blob(BlobToString(key_blob));
  certified_key.set_public_key(BlobToString(public_key_der));
  certified_key.set_public_key_tpm_format(serialized_public_key);
  certified_key.set_certified_key_info(certify_key_result.certify_info);
  certified_key.set_certified_key_proof(certify_key_result.signature);
  certified_key.set_key_type(key_type);
  certified_key.set_key_usage(key_usage);

  return certified_key;
}

}  // namespace hwsec
