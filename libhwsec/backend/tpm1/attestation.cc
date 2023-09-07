// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libhwsec/backend/tpm1/attestation.h"

#include <arpa/inet.h>
#include <base/hash/sha1.h>
#include <base/memory/free_deleter.h>
#include <base/sys_byteorder.h>
#include <memory>
#include <string>

#include <attestation/proto_bindings/attestation_ca.pb.h>
#include <attestation/proto_bindings/database.pb.h>
#include <brillo/secure_blob.h>
#include <crypto/scoped_openssl_types.h>
#include <libhwsec-foundation/status/status_chain_macros.h>
#include <libhwsec-foundation/crypto/rsa.h>

#include "libhwsec/backend/tpm1/static_utils.h"
#include "libhwsec/error/tpm1_error.h"
#include "libhwsec/overalls/overalls.h"
#include "libhwsec/status.h"
#include "libhwsec/structures/key.h"
#include "libhwsec/structures/operation_policy.h"

using brillo::BlobFromString;
using brillo::BlobToString;
using hwsec_foundation::status::MakeStatus;

namespace hwsec {

namespace {

using ScopedByteArray = std::unique_ptr<BYTE, base::FreeDeleter>;

constexpr unsigned int kDefaultTpmRsaKeyBits = 2048;
constexpr unsigned int kDefaultTpmRsaKeyFlag = TSS_KEY_SIZE_2048;
constexpr unsigned int kDigestSize = sizeof(TPM_DIGEST);
constexpr size_t kSelectBitmapSize = 2;

std::string TSSBufferAsString(const BYTE* buffer, size_t length) {
  return std::string(reinterpret_cast<const char*>(buffer), length);
}

// Builds the serialized TPM_PCR_COMPOSITE stream, where |pcr_index| is the PCR
// index, and |quoted_pcr_value| is the value of the register.
StatusOr<std::string> buildPcrComposite(uint32_t pcr_index,
                                        const std::string& quoted_pcr_value) {
  if (pcr_index >= kSelectBitmapSize * 8) {
    return MakeStatus<TPMError>("PCR index is out of range",
                                TPMRetryAction::kNoRetry);
  }
  // Builds the PCR composite header.
  struct __attribute__((packed)) {
    // Corresponding to TPM_PCR_SELECTION.sizeOfSelect.
    uint16_t select_size;
    // Corresponding to TPM_PCR_SELECTION.pcrSelect.
    uint8_t select_bitmap[kSelectBitmapSize];
    // Corresponding to  TPM_PCR_COMPOSITE.valueSize.
    uint32_t value_size;
  } composite_header = {0};
  static_assert(sizeof(composite_header) ==
                    sizeof(uint16_t) + kSelectBitmapSize + sizeof(uint32_t),
                "Expect no padding between composite struct.");
  // Sets to 2 bytes.
  composite_header.select_size = base::HostToNet16(2u);
  composite_header.select_bitmap[pcr_index / 8] = 1 << (pcr_index % 8);
  composite_header.value_size = base::HostToNet32(quoted_pcr_value.size());
  const char* buffer = reinterpret_cast<const char*>(&composite_header);
  return std::string(buffer, sizeof(composite_header)) + quoted_pcr_value;
}

StatusOr<brillo::Blob> GetAttribData(overalls::Overalls& overalls,
                                     TSS_HCONTEXT context,
                                     TSS_HOBJECT object,
                                     TSS_FLAG flag,
                                     TSS_FLAG sub_flag) {
  uint32_t length = 0;
  ScopedTssMemory buf(overalls, context);

  RETURN_IF_ERROR(MakeStatus<TPM1Error>(overalls.Ospi_GetAttribData(
                      object, flag, sub_flag, &length, buf.ptr())))
      .WithStatus<TPMError>("Failed to call Ospi_GetAttribData");

  return brillo::Blob(buf.value(), buf.value() + length);
}

StatusOr<std::string> DecryptIdentityRequest(overalls::Overalls& overalls,
                                             RSA* pca_key,
                                             const std::string& request) {
  // Parse the serialized TPM_IDENTITY_REQ structure.
  UINT64 offset = 0;
  BYTE* buffer = reinterpret_cast<BYTE*>(
      const_cast<typename std::string::value_type*>(request.data()));
  TPM_IDENTITY_REQ request_parsed;
  RETURN_IF_ERROR(MakeStatus<TPM1Error>(overalls.Orspi_UnloadBlob_IDENTITY_REQ(
                      &offset, buffer, &request_parsed)))
      .WithStatus<TPMError>("Failed to call Ospi_UnloadBlob_IDENTITY_REQ");
  ScopedByteArray scoped_asym_blob(request_parsed.asymBlob);
  ScopedByteArray scoped_sym_blob(request_parsed.symBlob);

  // Decrypt the symmetric key.
  unsigned char key_buffer[kDefaultTpmRsaKeyBits / 8];
  int key_length =
      RSA_private_decrypt(request_parsed.asymSize, request_parsed.asymBlob,
                          key_buffer, pca_key, RSA_PKCS1_PADDING);
  if (key_length == -1) {
    return MakeStatus<TPMError>("Failed to decrypt identity request key",
                                TPMRetryAction::kNoRetry);
  }
  TPM_SYMMETRIC_KEY symmetric_key;
  offset = 0;
  RETURN_IF_ERROR(MakeStatus<TPM1Error>(overalls.Orspi_UnloadBlob_SYMMETRIC_KEY(
                      &offset, key_buffer, &symmetric_key)))
      .WithStatus<TPMError>("Failed to call Ospi_UnloadBlob_SYMMETRIC_KEY");
  ScopedByteArray scoped_sym_key(symmetric_key.data);

  // Decrypt the request with the symmetric key.
  brillo::SecureBlob proof_serial;
  proof_serial.resize(request_parsed.symSize);
  UINT32 proof_serial_length = proof_serial.size();
  RETURN_IF_ERROR(
      MakeStatus<TPM1Error>(overalls.Orspi_SymDecrypt(
          symmetric_key.algId, TPM_ES_SYM_CBC_PKCS5PAD, symmetric_key.data,
          NULL, request_parsed.symBlob, request_parsed.symSize,
          proof_serial.data(), &proof_serial_length)))
      .WithStatus<TPMError>("Failed to call Orspi_SymDecrypt");

  // Parse the serialized TPM_IDENTITY_PROOF structure.
  TPM_IDENTITY_PROOF proof;
  offset = 0;
  RETURN_IF_ERROR(
      MakeStatus<TPM1Error>(overalls.Orspi_UnloadBlob_IDENTITY_PROOF(
          &offset, proof_serial.data(), &proof)))
      .WithStatus<TPMError>("Failed to call Orspi_UnloadBlob_IDENTITY_PROOF");
  ScopedByteArray scoped_label(proof.labelArea);
  ScopedByteArray scoped_binding(proof.identityBinding);
  ScopedByteArray scoped_endorsement(proof.endorsementCredential);
  ScopedByteArray scoped_platform(proof.platformCredential);
  ScopedByteArray scoped_conformance(proof.conformanceCredential);
  ScopedByteArray scoped_key(proof.identityKey.pubKey.key);
  ScopedByteArray scoped_parms(proof.identityKey.algorithmParms.parms);

  std::string identity_binding(
      proof.identityBinding, proof.identityBinding + proof.identityBindingSize);
  brillo::SecureClearBytes(proof.identityBinding, proof.identityBindingSize);
  return identity_binding;
}

}  // namespace

StatusOr<attestation::Quote> AttestationTpm1::Quote(
    DeviceConfigs device_configs, Key key) {
  if (device_configs.none()) {
    return MakeStatus<TPMError>("Quote with no device config specified",
                                TPMRetryAction::kNoRetry);
  }

  attestation::Quote quote;
  ASSIGN_OR_RETURN(const ConfigTpm1::PcrMap& pcr_map,
                   config_.ToPcrMap(device_configs),
                   _.WithStatus<TPMError>("Failed to get PCR map"));
  if (pcr_map.size() == 1) {
    int pcr = pcr_map.begin()->first;
    ASSIGN_OR_RETURN(const brillo::Blob& value, config_.ReadPcr(pcr),
                     _.WithStatus<TPMError>("Failed to read PCR"));
    quote.set_quoted_pcr_value(BlobToString(value));
  }

  ASSIGN_OR_RETURN(ScopedTssPcrs && pcr_select,
                   config_.ToPcrSelection(device_configs),
                   _.WithStatus<TPMError>(
                       "Failed to convert device configs to PCR selection"));

  ASSIGN_OR_RETURN(const KeyTpm1& key_data, key_management_.GetKeyData(key));
  ASSIGN_OR_RETURN(TSS_HTPM tpm_handle, tss_helper_.GetTpmHandle());
  ASSIGN_OR_RETURN(TSS_HCONTEXT context, tss_helper_.GetTssContext());
  // Generate the quote.
  TSS_VALIDATION validation = {};
  // Here we use well-known string value for consistency with AttestationTpm2,
  // which doesn't supply any qualifying data from caller while in TPM 1.2
  // it's required to have non-empty external data.
  BYTE well_known_external_data[kDigestSize] = {};
  validation.ulExternalDataLength = kDigestSize;
  validation.rgbExternalData = well_known_external_data;
  RETURN_IF_ERROR(
      MakeStatus<TPM1Error>(overalls_.Ospi_TPM_Quote(
          tpm_handle, key_data.key_handle, pcr_select, &validation)))
      .WithStatus<TPMError>("Failed to call Ospi_TPM_Quote");
  ScopedTssMemory scoped_signed_data(overalls_, context, validation.rgbData);
  ScopedTssMemory scoped_signature(overalls_, context,
                                   validation.rgbValidationData);

  if (device_configs[DeviceConfig::kDeviceModel]) {
    if (StatusOr<std::string> hwid = config_.GetHardwareID(); !hwid.ok()) {
      LOG(WARNING) << "Failed to get Hardware ID: " << hwid.status();
    } else {
      quote.set_pcr_source_hint(hwid.value());
    }
  }
  quote.set_quoted_data(std::string(
      validation.rgbData, validation.rgbData + validation.ulDataLength));
  quote.set_quote(std::string(
      validation.rgbValidationData,
      validation.rgbValidationData + validation.ulValidationDataLength));

  return quote;
}

StatusOr<bool> AttestationTpm1::IsQuoted(DeviceConfigs device_configs,
                                         const attestation::Quote& quote) {
  if (device_configs.none()) {
    return MakeStatus<TPMError>("No device config specified",
                                TPMRetryAction::kNoRetry);
  }
  if (device_configs.count() > 1) {
    return MakeStatus<TPMError>(
        "Verifying quote for Multiple device configs is unsupported",
        TPMRetryAction::kNoRetry);
  }
  if (!quote.has_quoted_pcr_value() || !quote.has_quoted_data()) {
    return MakeStatus<TPMError>("Invalid attestation::Quote",
                                TPMRetryAction::kNoRetry);
  }

  ASSIGN_OR_RETURN(const ConfigTpm1::PcrMap& pcr_map,
                   config_.ToPcrMap(device_configs),
                   _.WithStatus<TPMError>("Failed to get PCR map"));
  if (pcr_map.size() != 1) {
    return MakeStatus<TPMError>("Wrong number of PCR specified",
                                TPMRetryAction::kNoRetry);
  }

  ASSIGN_OR_RETURN(
      const std::string& pcr_composite,
      buildPcrComposite(pcr_map.begin()->first, quote.quoted_pcr_value()));
  // Checks that the quoted value matches the given PCR value by reconstructing
  // the TPM_PCR_COMPOSITE structure the TPM would create.
  const std::string pcr_digest = base::SHA1HashString(pcr_composite);

  const std::string signed_data = quote.quoted_data();
  // The PCR digest should appear starting at 8th byte of the quoted data. See
  // the TPM_QUOTE_INFO structure.
  if (signed_data.length() < pcr_digest.length() + 8) {
    return MakeStatus<TPMError>("Quoted data is too short",
                                TPMRetryAction::kNoRetry);
  }
  if (!std::equal(pcr_digest.begin(), pcr_digest.end(),
                  signed_data.begin() + 8)) {
    return false;
  }
  return true;
}

StatusOr<AttestationTpm1::CertifyKeyResult> AttestationTpm1::CertifyKey(
    Key key, Key identity_key, const std::string& external_data) {
  ASSIGN_OR_RETURN(TSS_HCONTEXT context, tss_helper_.GetTssContext());
  ASSIGN_OR_RETURN(const KeyTpm1& key_data, key_management_.GetKeyData(key),
                   _.WithStatus<TPMError>("Failed to get the key data"));
  ASSIGN_OR_RETURN(
      const KeyTpm1& identity_key_data,
      key_management_.GetKeyData(identity_key),
      _.WithStatus<TPMError>("Failed to get the identity key data"));
  TSS_HKEY key_handle = key_data.key_handle;
  TSS_HKEY identity_key_handle = identity_key_data.key_handle;

  TSS_VALIDATION validation;
  memset(&validation, 0, sizeof(validation));
  validation.ulExternalDataLength = external_data.size();
  brillo::Blob mutable_external_data = BlobFromString(external_data);
  validation.rgbExternalData = mutable_external_data.data();

  RETURN_IF_ERROR(MakeStatus<TPM1Error>(overalls_.Ospi_Key_CertifyKey(
                      key_handle, identity_key_handle, &validation)))
      .WithStatus<TPMError>("Failed to call Ospi_Key_CertifyKey");
  ScopedTssMemory scoped_certified_data(overalls_, context, validation.rgbData);
  ScopedTssMemory scoped_proof(overalls_, context,
                               validation.rgbValidationData);

  return CertifyKeyResult{
      .certify_info =
          TSSBufferAsString(validation.rgbData, validation.ulDataLength),
      .signature = TSSBufferAsString(validation.rgbValidationData,
                                     validation.ulValidationDataLength)};
}

StatusOr<attestation::CertifiedKey> AttestationTpm1::CreateCertifiedKey(
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
    default:
      return MakeStatus<TPMError>("Unsupported key algorithm type",
                                  TPMRetryAction::kNoRetry);
  }
  if (restriction == KeyRestriction::kRestricted) {
    return MakeStatus<TPMError>("Unsupported restricted key",
                                TPMRetryAction::kNoRetry);
  }
  if (endorsement_auth == EndorsementAuth::kEndorsement) {
    return MakeStatus<TPMError>("Unsupported using endorsement hierarchy",
                                TPMRetryAction::kNoRetry);
  }

  ASSIGN_OR_RETURN(
      const KeyManagement::CreateKeyResult& create_key_result,
      key_management_.CreateKey(
          OperationPolicySetting{}, key_algo,
          KeyManagement::LoadKeyOptions{.auto_reload = true},
          KeyManagement::CreateKeyOptions{
              .allow_software_gen = false,
              .allow_decrypt =
                  key_usage == attestation::KeyUsage::KEY_USAGE_DECRYPT,
              .allow_sign = key_usage == attestation::KeyUsage::KEY_USAGE_SIGN,
          }),
      _.WithStatus<TPMError>("Failed to create key"));
  const ScopedKey& key = create_key_result.key;
  const brillo::Blob& key_blob = create_key_result.key_blob;

  ASSIGN_OR_RETURN(const CertifyKeyResult& certify_key_result,
                   CertifyKey(key.GetKey(), identity_key, external_data),
                   _.WithStatus<TPMError>("Failed to certify key"));

  ASSIGN_OR_RETURN(const KeyTpm1& key_data,
                   key_management_.GetKeyData(key.GetKey()),
                   _.WithStatus<TPMError>("Failed to get key data"));
  std::string serialized_public_key = BlobToString(key_data.cache.pubkey_blob);

  ASSIGN_OR_RETURN(
      const brillo::Blob& public_key_der,
      key_management_.GetPublicKeyDer(key.GetKey()),
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

StatusOr<Attestation::CreateIdentityResult> AttestationTpm1::CreateIdentity(
    attestation::KeyType key_type) {
  if (key_type != attestation::KEY_TYPE_RSA) {
    return MakeStatus<TPMError>("non-RSA identity key is unsupported",
                                TPMRetryAction::kNoRetry);
  }
  ASSIGN_OR_RETURN(TSS_HCONTEXT context, tss_helper_.GetTssContext());
  ASSIGN_OR_RETURN(TSS_HTPM tpm_handle, tss_helper_.GetTpmHandle());
  ASSIGN_OR_RETURN(base::ScopedClosureRunner owner_handle_cleanup,
                   tss_helper_.SetTpmHandleAsOwner());

  // Create fake PCA key.
  crypto::ScopedRSA fake_pca_key =
      hwsec_foundation::GenerateRsa(kDefaultTpmRsaKeyBits);
  if (!fake_pca_key) {
    return MakeStatus<TPMError>("Failed to generate fake pca key",
                                TPMRetryAction::kNoRetry);
  }

  brillo::Blob modulus(RSA_size(fake_pca_key.get()));
  const BIGNUM* n;
  RSA_get0_key(fake_pca_key.get(), &n, nullptr, nullptr);
  if (BN_bn2bin(n, modulus.data()) != modulus.size()) {
    return MakeStatus<TPMError>("RSA modulus size mismatch",
                                TPMRetryAction::kNoRetry);
  }

  // Create a TSS object for the fake PCA public key.
  TSS_FLAG pca_key_flags =
      kDefaultTpmRsaKeyFlag | TSS_KEY_TYPE_LEGACY | TSS_KEY_MIGRATABLE;
  ASSIGN_OR_RETURN(
      ScopedKey pca_public_key_object,
      key_management_.CreateRsaPublicKeyObject(
          modulus, pca_key_flags, TSS_SS_NONE, TSS_ES_RSAESPKCSV15),
      _.WithStatus<TPMError>("Failed to create PCA public key info"));
  ASSIGN_OR_RETURN(const KeyTpm1& pca_public_key_data,
                   key_management_.GetKeyData(pca_public_key_object.GetKey()),
                   _.WithStatus<TPMError>("Failed to get PCA public key data"));
  if (!pca_public_key_data.scoped_key.has_value()) {
    return MakeStatus<TPMError>("Missing scoped key in PCA public key data",
                                TPMRetryAction::kNoRetry);
  }
  TSS_HKEY pca_public_key = pca_public_key_data.scoped_key.value().value();

  // Get the fake PCA public key in serialized TPM_PUBKEY form.
  ASSIGN_OR_RETURN(
      const brillo::Blob& pca_public_key_blob,
      GetAttribData(overalls_, context, pca_public_key, TSS_TSPATTRIB_KEY_BLOB,
                    TSS_TSPATTRIB_KEYBLOB_PUBLIC_KEY),
      _.WithStatus<TPMError>("Failed to get serilized PCA public key"));

  // Construct an arbitrary unicode label.
  const char* label_text = "ChromeOS_AIK_1BJNAMQDR4RH44F4ET2KPAOMJMO043K1";
  BYTE* label_ascii =
      const_cast<BYTE*>(reinterpret_cast<const BYTE*>(label_text));
  unsigned int label_size = strlen(label_text);
  ScopedByteArray scoped_label(
      overalls_.Orspi_Native_To_UNICODE(label_ascii, &label_size));
  if (!scoped_label.get()) {
    return MakeStatus<TPMError>("Failed to create AIK label",
                                TPMRetryAction::kNoRetry);
  }
  BYTE* label = scoped_label.get();
  std::string identity_label(label, label + label_size);

  // Initialize a key object to hold the new identity key.
  ScopedTssKey identity_key(overalls_, context);
  TSS_FLAG identity_key_flags = kDefaultTpmRsaKeyFlag | TSS_KEY_TYPE_IDENTITY |
                                TSS_KEY_VOLATILE | TSS_KEY_NOT_MIGRATABLE;
  RETURN_IF_ERROR(MakeStatus<TPM1Error>(overalls_.Ospi_Context_CreateObject(
                      context, TSS_OBJECT_TYPE_RSAKEY, identity_key_flags,
                      identity_key.ptr())))
      .WithStatus<TPMError>("Failed to create identity key object");

  // Get Storage Root Key (SRK).
  ASSIGN_OR_RETURN(ScopedKey srk,
                   key_management_.GetPersistentKey(
                       KeyManagement::PersistentKeyType::kStorageRootKey));
  ASSIGN_OR_RETURN(const KeyTpm1& srk_data,
                   key_management_.GetKeyData(srk.GetKey()));

  // Create the identity and receive the request intended for the PCA.
  uint32_t request_length = 0;
  ScopedTssMemory request(overalls_, context);
  RETURN_IF_ERROR(
      MakeStatus<TPM1Error>(overalls_.Ospi_TPM_CollateIdentityRequest(
          tpm_handle, srk_data.key_handle, pca_public_key, label_size, label,
          identity_key, TSS_ALG_3DES, &request_length, request.ptr())))
      .WithStatus<TPMError>("Failed to make identity");

  // Decrypt and parse the identity request.
  std::string request_blob(request.value(), request.value() + request_length);
  ASSIGN_OR_RETURN(
      const std::string& identity_binding,
      DecryptIdentityRequest(overalls_, fake_pca_key.get(), request_blob),
      _.WithStatus<TPMError>("Failed to decrypt the identity request"));
  brillo::SecureClearBytes(request.value(), request_length);

  // Get the AIK public key.
  ASSIGN_OR_RETURN(
      const brillo::Blob& identity_public_key,
      GetAttribData(overalls_, context, identity_key, TSS_TSPATTRIB_KEY_BLOB,
                    TSS_TSPATTRIB_KEYBLOB_PUBLIC_KEY),
      _.WithStatus<TPMError>("Failed to get identity key blob"));
  ASSIGN_OR_RETURN(
      const brillo::Blob& identity_public_key_der,
      key_management_.GetPublicKeyDerFromBlob(identity_public_key),
      _.WithStatus<TPMError>("Failed to get DER-encoded identity public key"));

  // Get the AIK blob so we can load it later.
  ASSIGN_OR_RETURN(
      const brillo::Blob& identity_key_blob,
      GetAttribData(overalls_, context, identity_key, TSS_TSPATTRIB_KEY_BLOB,
                    TSS_TSPATTRIB_KEYBLOB_BLOB),
      _.WithStatus<TPMError>("Failed to get identity key blob"));

  // Fill the fields in CreateIdentityResult
  attestation::IdentityKey identity_key_info;
  identity_key_info.set_identity_key_type(key_type);
  identity_key_info.set_identity_public_key_der(
      BlobToString(identity_public_key_der));
  identity_key_info.set_identity_key_blob(BlobToString(identity_key_blob));

  attestation::IdentityBinding identity_binding_info;
  identity_binding_info.set_identity_public_key_tpm_format(
      BlobToString(identity_public_key));
  identity_binding_info.set_identity_binding(identity_binding);
  identity_binding_info.set_pca_public_key(BlobToString(pca_public_key_blob));
  identity_binding_info.set_identity_label(identity_label);
  identity_binding_info.set_identity_public_key_der(
      BlobToString(identity_public_key_der));

  return Attestation::CreateIdentityResult{
      .identity_key = identity_key_info,
      .identity_binding = identity_binding_info,
  };
}

}  // namespace hwsec
