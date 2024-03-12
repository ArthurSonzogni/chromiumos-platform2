// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libhwsec/backend/tpm2/attestation.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <attestation/proto_bindings/attestation_ca.pb.h>
#include <attestation/proto_bindings/database.pb.h>
#include <base/strings/stringprintf.h>
#include <crypto/scoped_openssl_types.h>
#include <crypto/secure_util.h>
#include <crypto/sha2.h>
#include <trunks/mock_tpm_utility.h>
#include <trunks/multiple_authorization_delegate.h>
#include <libhwsec-foundation/crypto/aes.h>
#include <libhwsec-foundation/crypto/hmac.h>
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
using Mode = hwsec::DeviceConfigSettings::BootModeSetting::Mode;

namespace hwsec {
namespace {

constexpr size_t kRandomCertifiedKeyPasswordLength = 32;
const char kHashHeaderForEncrypt[] = "ENCRYPT";
const char kHashHeaderForMac[] = "MAC";

std::string GetDescriptionForMode(const Mode& mode) {
  return base::StringPrintf(
      "(Developer Mode: %s, Recovery Mode: %s, Firmware Type: %s)",
      mode.developer_mode ? "On" : "Off", mode.recovery_mode ? "On" : "Off",
      mode.verified_firmware ? "Verified" : "Developer");
}

StatusOr<brillo::SecureBlob> DecryptIdentityCertificate(
    const std::string& credential, const attestation::EncryptedData& input) {
  brillo::SecureBlob aes_key(
      crypto::SHA256HashString(kHashHeaderForEncrypt + credential));
  brillo::SecureBlob hmac_key(
      crypto::SHA256HashString(kHashHeaderForMac + credential));
  brillo::SecureBlob expected_mac = hwsec_foundation::HmacSha512(
      hmac_key, brillo::SecureBlob(input.iv() + input.encrypted_data()));
  if (expected_mac.size() != input.mac().size()) {
    return MakeStatus<TPMError>("MAC size mismatch", TPMRetryAction::kNoRetry);
  }
  if (!crypto::SecureMemEqual(expected_mac.data(), input.mac().data(),
                              expected_mac.size())) {
    return MakeStatus<TPMError>("MAC mismatch", TPMRetryAction::kNoRetry);
  }
  brillo::SecureBlob decrypted;
  brillo::Blob encrypted = BlobFromString(input.encrypted_data());
  brillo::Blob iv = BlobFromString(input.iv());
  if (!hwsec_foundation::AesDecryptSpecifyBlockMode(
          encrypted, 0, encrypted.size(), brillo::SecureBlob(aes_key), iv,
          hwsec_foundation::PaddingScheme::kPaddingStandard,
          hwsec_foundation::BlockMode::kCbc, &decrypted)) {
    return MakeStatus<TPMError>("AES Decryption failed",
                                TPMRetryAction::kNoRetry);
  }
  return decrypted;
}

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
  if (device_configs[DeviceConfig::kBootMode]) {
    ASSIGN_OR_RETURN(Mode mode, config_.GetCurrentBootMode(),
                     _.WithStatus<TPMError>("Failed to get current boot mode"));
    ASSIGN_OR_RETURN(Mode quoted_mode,
                     config_.ToBootMode(quote.quoted_pcr_value()),
                     _.WithStatus<TPMError>("Failed to get quoted boot mode"));
    if (mode != quoted_mode) {
      std::string err_msg = base::StringPrintf(
          "Quoted boot mode mismatched: current %s vs quoted %s",
          GetDescriptionForMode(mode).c_str(),
          GetDescriptionForMode(quoted_mode).c_str());
      return MakeStatus<TPMError>(err_msg, TPMRetryAction::kNoRetry);
    }
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

StatusOr<Attestation::CreateIdentityResult> AttestationTpm2::CreateIdentity(
    attestation::KeyType key_type) {
  trunks::TPM_ALG_ID algorithm;
  switch (key_type) {
    case attestation::KEY_TYPE_RSA:
      algorithm = trunks::TPM_ALG_RSA;
      break;
    case attestation::KEY_TYPE_ECC:
      algorithm = trunks::TPM_ALG_ECC;
      break;
    default:
      return MakeStatus<TPMError>("Unsupported key algorithm type",
                                  TPMRetryAction::kNoRetry);
  }

  std::unique_ptr<trunks::AuthorizationDelegate> delegate =
      context_.GetTrunksFactory().GetPasswordAuthorization("");
  std::string identity_key_blob;
  RETURN_IF_ERROR(
      MakeStatus<TPM2Error>(context_.GetTpmUtility().CreateIdentityKey(
          algorithm, delegate.get(), &identity_key_blob)))
      .WithStatus<TPMError>("Failed to call CreateIdentityKey");

  std::unique_ptr<trunks::BlobParser> parser =
      context_.GetTrunksFactory().GetBlobParser();
  trunks::TPM2B_PUBLIC public_info;
  trunks::TPM2B_PRIVATE not_used;
  if (!parser->ParseKeyBlob(identity_key_blob, &public_info, &not_used)) {
    return MakeStatus<TPMError>("Failed to parse key blob",
                                TPMRetryAction::kNoRetry);
  }
  const trunks::TPMT_PUBLIC& public_data = public_info.public_area;

  std::string serialized_public_key;
  RETURN_IF_ERROR(MakeStatus<TPM2Error>(trunks::Serialize_TPMT_PUBLIC(
                      public_data, &serialized_public_key)))
      .WithStatus<TPMError>("Failed to serialized TPMT_PUBLIC");

  ASSIGN_OR_RETURN(
      const brillo::Blob& public_key_der,
      key_management_.GetPublicKeyDerFromPublicData(public_data, false),
      _.WithStatus<TPMError>("Failed to get public key in DER format"));

  attestation::IdentityKey identity_key_info;
  identity_key_info.set_identity_key_type(key_type);
  identity_key_info.set_identity_public_key_der(BlobToString(public_key_der));
  identity_key_info.set_identity_key_blob(identity_key_blob);

  attestation::IdentityBinding identity_binding_info;
  identity_binding_info.set_identity_public_key_tpm_format(
      serialized_public_key);
  identity_binding_info.set_identity_public_key_der(
      BlobToString(public_key_der));

  return Attestation::CreateIdentityResult{
      .identity_key = identity_key_info,
      .identity_binding = identity_binding_info,
  };
}

StatusOr<brillo::SecureBlob> AttestationTpm2::ActivateIdentity(
    attestation::KeyType key_type,
    Key identity_key,
    const attestation::EncryptedIdentityCredential& encrypted_certificate) {
  KeyAlgoType key_algo;
  switch (key_type) {
    case attestation::KEY_TYPE_RSA:
      key_algo = hwsec::KeyAlgoType::kRsa;
      break;
    case attestation::KEY_TYPE_ECC:
      key_algo = hwsec::KeyAlgoType::kEcc;
      break;
    default:
      return MakeStatus<TPMError>("Unsupported key algorithm type",
                                  TPMRetryAction::kNoRetry);
  }
  std::unique_ptr<trunks::AuthorizationDelegate> delegate =
      context_.GetTrunksFactory().GetPasswordAuthorization("");

  ASSIGN_OR_RETURN(ScopedKey endorsment_key,
                   key_management_.GetEndorsementKey(key_algo));

  ASSIGN_OR_RETURN(
      const KeyTpm2& endorsement_key_data,
      key_management_.GetKeyData(endorsment_key.GetKey()),
      _.WithStatus<TPMError>("Failed to get endorsement key data"));
  ASSIGN_OR_RETURN(const KeyTpm2& identity_key_data,
                   key_management_.GetKeyData(identity_key),
                   _.WithStatus<TPMError>("Failed to get identity key data"));
  const trunks::TPM_HANDLE& endorsement_key_handle =
      endorsement_key_data.key_handle;
  const trunks::TPM_HANDLE& identity_key_handle = identity_key_data.key_handle;

  std::string endorsement_key_name;
  std::string identity_key_name;
  RETURN_IF_ERROR(MakeStatus<TPM2Error>(context_.GetTpmUtility().GetKeyName(
                      endorsement_key_handle, &endorsement_key_name)))
      .WithStatus<TPMError>("Failed to get endorsement key name");
  RETURN_IF_ERROR(MakeStatus<TPM2Error>(context_.GetTpmUtility().GetKeyName(
                      identity_key_handle, &identity_key_name)))
      .WithStatus<TPMError>("Failed to get identity key name");

  ASSIGN_OR_RETURN(
      const brillo::SecureBlob& endorsement_password,
      GetEndorsementPassword(tpm_manager_),
      _.WithStatus<TPMError>("Failed to get endorsement password"));
  std::unique_ptr<trunks::HmacSession> endorsement_session =
      context_.GetTrunksFactory().GetHmacSession();
  RETURN_IF_ERROR(
      MakeStatus<TPM2Error>(endorsement_session->StartUnboundSession(
          true /* salted */, false /* enable_encryption */)))
      .WithStatus<TPMError>("Failed to start hmac session");
  endorsement_session->SetEntityAuthorizationValue(
      endorsement_password.to_string());

  ASSIGN_OR_RETURN(std::unique_ptr<trunks::PolicySession> session,
                   config_.GetTrunksPolicySession(OperationPolicy{},
                                                  std::vector<std::string>(),
                                                  /*salted=*/true,
                                                  /*enable_encryption=*/false),
                   _.WithStatus<TPMError>("Failed to get session for policy"));

  trunks::TPMI_DH_ENTITY auth_entity = trunks::TPM_RH_ENDORSEMENT;
  std::string auth_entity_name;
  trunks::Serialize_TPM_HANDLE(auth_entity, &auth_entity_name);

  RETURN_IF_ERROR(
      MakeStatus<TPM2Error>(session->PolicySecret(
          auth_entity, auth_entity_name, std::string(), std::string(),
          std::string(), 0, endorsement_session->GetDelegate())))
      .WithStatus<TPMError>("Failed to set secrete");

  trunks::MultipleAuthorizations authorization;
  authorization.AddAuthorizationDelegate(delegate.get());
  authorization.AddAuthorizationDelegate(session->GetDelegate());
  std::string identity_object_data;
  trunks::Serialize_TPM2B_DIGEST(
      trunks::Make_TPM2B_DIGEST(encrypted_certificate.credential_mac()),
      &identity_object_data);
  identity_object_data +=
      encrypted_certificate.wrapped_certificate().wrapped_key();
  trunks::TPM2B_DIGEST encoded_credential;
  RETURN_IF_ERROR(
      MakeStatus<TPM2Error>(
          context_.GetTrunksFactory().GetTpm()->ActivateCredentialSync(
              identity_key_handle, identity_key_name, endorsement_key_handle,
              endorsement_key_name,
              trunks::Make_TPM2B_ID_OBJECT(identity_object_data),
              trunks::Make_TPM2B_ENCRYPTED_SECRET(
                  encrypted_certificate.encrypted_seed()),
              &encoded_credential, &authorization)))
      .WithStatus<TPMError>("Failed to activate credential");
  std::string credential = trunks::StringFrom_TPM2B_DIGEST(encoded_credential);
  return DecryptIdentityCertificate(
      credential, encrypted_certificate.wrapped_certificate());
}

}  // namespace hwsec
