// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/recoverable_key_store/generate.h"

#include <array>
#include <bit>
#include <iterator>
#include <optional>
#include <utility>

#include <base/sys_byteorder.h>
#include <brillo/secure_blob.h>
#include <cryptohome/proto_bindings/recoverable_key_store.pb.h>
#include <cryptohome/proto_bindings/UserDataAuth.pb.h>
#include <libhwsec-foundation/crypto/aes.h>
#include <libhwsec-foundation/crypto/secure_blob_util.h>
#include <libhwsec-foundation/crypto/secure_box.h>
#include <libhwsec-foundation/crypto/sha.h>

#include "cryptohome/error/cryptohome_error.h"
#include "cryptohome/error/location_utils.h"
#include "cryptohome/recoverable_key_store/type.h"

namespace cryptohome {
namespace {

using ::cryptohome::error::CryptohomeError;
using ::cryptohome::error::ErrorActionSet;
using ::cryptohome::error::PossibleAction;
using ::hwsec_foundation::AesGcmEncrypt;
using ::hwsec_foundation::CreateRandomBlob;
using ::hwsec_foundation::CreateSecureRandomBlob;
using ::hwsec_foundation::Sha256;
using ::hwsec_foundation::status::MakeStatus;

constexpr size_t kRecoveryKeySize = 256 / CHAR_BIT;
constexpr size_t kCrOSRecoverableKeyStoreHandleBodySize = 16;
// Use the same max failed attempts as Android.
constexpr uint32_t kRecoveryMaxFailedAttempts = 10;
// The max attempts should be serialized in little-endian byte representation.
constexpr auto kRecoveryMaxFailedAttemptsLeBytes =
    std::bit_cast<std::array<uint8_t, 4>>(
        base::ByteSwapToLE32(kRecoveryMaxFailedAttempts));

constexpr char kSecurityDomainKeyName[] =
    "security_domain_member_key_encrypted_locally";
constexpr char kLocallyEncryptedRecoveryKeyHeader[] =
    "V1 locally_encrypted_recovery_key";
constexpr char kThmEncryptedRecoveryKeyHeader[] =
    "V1 THM_encrypted_recovery_key";
constexpr char kThmKhHashPrefix[] = "THM_KF_hash";
constexpr uint8_t kCrOSRecoverableKeyStoreHandleHeader[] = {0x02};

std::optional<WrappedSecurityDomainKey> GenerateWrappedSecurityDomainKey(
    const SecurityDomainKeys& keys, const brillo::SecureBlob& recovery_key) {
  // Wrap the security domain private key by the security domain wrapping key.
  brillo::Blob iv, tag, wrapped_private_key;
  if (!AesGcmEncrypt(keys.key_pair.private_key, std::nullopt, keys.wrapping_key,
                     &iv, &tag, &wrapped_private_key)) {
    LOG(ERROR) << "Failed to wrap security domain private key.";
    return std::nullopt;
  }

  // Wrap the security domain wrapping key by the recovery key.
  std::optional<brillo::Blob> wrapped_wrapping_key =
      hwsec_foundation::secure_box::Encrypt(brillo::Blob(), recovery_key,
                                            brillo::Blob(), keys.wrapping_key);
  if (!wrapped_wrapping_key.has_value()) {
    LOG(ERROR) << "Failed to wrap security domain wrapping key.";
    return std::nullopt;
  }

  WrappedSecurityDomainKey key_proto;
  key_proto.set_key_name(kSecurityDomainKeyName);
  key_proto.set_public_key(brillo::BlobToString(keys.key_pair.public_key));
  key_proto.set_wrapped_private_key(brillo::BlobToString(wrapped_private_key));
  key_proto.set_wrapped_wrapping_key(
      brillo::BlobToString(*wrapped_wrapping_key));
  return key_proto;
}

std::optional<RecoverableKeyStoreMetadata> GenerateRecoverableKeyStoreMetadata(
    const KnowledgeFactor& knowledge_factor,
    const RecoverableKeyStoreBackendCert& cert) {
  RecoverableKeyStoreMetadata metadata;
  metadata.set_knowledge_factor_type(knowledge_factor.knowledge_factor_type);
  metadata.set_hash_type(knowledge_factor.algorithm);
  metadata.set_hash_salt(brillo::BlobToString(knowledge_factor.salt));
  metadata.set_cert_list_version(cert.version);
  return metadata;
}

struct RecoverableKeyStoreParametersRepresentations {
  // The proto format of the parameters to be put in RecoverableKeyStore.
  RecoverableKeyStoreParameters proto;
  // The serialized blob format to be used as a part of SecureBox encryption
  // header.
  brillo::Blob serialized;
};
std::optional<RecoverableKeyStoreParametersRepresentations>
GenerateRecoverableKeyStoreParameters(
    const brillo::Blob& wrong_attempt_label,
    const RecoverableKeyStoreBackendCert& cert) {
  // The key store handle is a fixed CrOS header + fixed-length random bytes.
  brillo::Blob recoverable_key_store_handle = brillo::CombineBlobs(
      {brillo::Blob(std::begin(kCrOSRecoverableKeyStoreHandleHeader),
                    std::end(kCrOSRecoverableKeyStoreHandleHeader)),
       CreateRandomBlob(kCrOSRecoverableKeyStoreHandleBodySize)});

  // Construct the proto.
  RecoverableKeyStoreParameters params_proto;
  params_proto.set_backend_public_key(brillo::BlobToString(cert.public_key));
  params_proto.set_counter_id(brillo::BlobToString(wrong_attempt_label));
  params_proto.set_max_attempts(kRecoveryMaxFailedAttempts);
  params_proto.set_key_store_handle(
      brillo::BlobToString(recoverable_key_store_handle));

  // Construct the serialized blob.
  brillo::Blob max_attempts(kRecoveryMaxFailedAttemptsLeBytes.begin(),
                            kRecoveryMaxFailedAttemptsLeBytes.end());
  brillo::Blob params_blob =
      brillo::CombineBlobs({cert.public_key, wrong_attempt_label, max_attempts,
                            recoverable_key_store_handle});

  return RecoverableKeyStoreParametersRepresentations{
      .proto = std::move(params_proto),
      .serialized = std::move(params_blob),
  };
}

std::optional<brillo::Blob> GenerateWrappedRecoveryKey(
    const brillo::SecureBlob& recovery_key,
    const KnowledgeFactor& knowledge_factor,
    const RecoverableKeyStoreBackendCert& cert,
    const brillo::Blob& key_store_params) {
  // First layer of encryption uses the knowledge factor as the key.
  // This ensures only possession of the knowledge factor grants access to the
  // recover key and therefore the security domain key backed by it.
  std::optional<brillo::Blob> knowledge_factor_wrapped_recovery_key =
      hwsec_foundation::secure_box::Encrypt(
          brillo::Blob(), knowledge_factor.hash,
          brillo::BlobFromString(kLocallyEncryptedRecoveryKeyHeader),
          recovery_key);
  if (!knowledge_factor_wrapped_recovery_key.has_value()) {
    LOG(ERROR) << "Failed to wrap recovery key by knowledge factor hash.";
    return std::nullopt;
  }

  // Second layer of encryption uses the recoverable key store service backend
  // public key. This ensures the decryption attempts using knowledge factor can
  // only be done in the service backend, such that:
  // 1. The wrong attempt limitation can be enforced properly.
  // 2. The key store blob doesn't become a material for attackers to
  // brute-force the user's knowledge factor value.
  brillo::SecureBlob hashed_knowledge_factor =
      Sha256(brillo::SecureBlob::Combine(brillo::SecureBlob(kThmKhHashPrefix),
                                         knowledge_factor.hash));
  brillo::Blob header = brillo::CombineBlobs(
      {brillo::BlobFromString(kThmEncryptedRecoveryKeyHeader),
       key_store_params});
  std::optional<brillo::Blob> wrapped_recovery_key =
      hwsec_foundation::secure_box::Encrypt(
          cert.public_key, hashed_knowledge_factor, header,
          brillo::SecureBlob(knowledge_factor_wrapped_recovery_key->begin(),
                             knowledge_factor_wrapped_recovery_key->end()));
  if (!wrapped_recovery_key.has_value()) {
    LOG(ERROR) << "Failed to wrap recovery key by backend public key.";
    return std::nullopt;
  }
  return std::move(*wrapped_recovery_key);
}

}  // namespace

CryptohomeStatusOr<RecoverableKeyStore> GenerateRecoverableKeyStore(
    const KnowledgeFactor& knowledge_factor,
    const brillo::Blob& wrong_attempt_label,
    const SecurityDomainKeys& keys,
    const RecoverableKeyStoreBackendCert& cert) {
  brillo::SecureBlob recovery_key = CreateSecureRandomBlob(kRecoveryKeySize);

  // Generate the 4 major fields of the RecoverableKeyStore proto separately,
  // using the input parameters and a randomly-generated recovery_key.

  std::optional<WrappedSecurityDomainKey> wrapped_security_domain_key =
      GenerateWrappedSecurityDomainKey(keys, recovery_key);
  if (!wrapped_security_domain_key.has_value()) {
    LOG(ERROR) << "Failed to generate wrapped security domain key.";
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocGenKeyStoreGenSecurityDomainKeyFailed),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED);
  }

  std::optional<RecoverableKeyStoreMetadata> key_store_metadata =
      GenerateRecoverableKeyStoreMetadata(knowledge_factor, cert);
  if (!key_store_metadata.has_value()) {
    LOG(ERROR) << "Failed to generate recoverable key store metadata.";
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocGenKeyStoreGenKeyStoreMetadataFailed),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED);
  }

  std::optional<RecoverableKeyStoreParametersRepresentations> key_store_params =
      GenerateRecoverableKeyStoreParameters(wrong_attempt_label, cert);
  if (!key_store_params.has_value()) {
    LOG(ERROR) << "Failed to generate recoverable key store params.";
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocGenKeyStoreGenKeyStoreParamsFailed),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED);
  }

  std::optional<brillo::Blob> wrapped_recovery_key = GenerateWrappedRecoveryKey(
      recovery_key, knowledge_factor, cert, key_store_params->serialized);
  if (!wrapped_recovery_key.has_value()) {
    LOG(ERROR) << "Failed to generate wrapped recovery key.";
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocGenKeyStoreGenWrappedRecoveryKeyFailed),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED);
  }

  RecoverableKeyStore key_store;
  *key_store.mutable_key_store_parameters() =
      std::move(key_store_params->proto);
  *key_store.mutable_key_store_metadata() = std::move(*key_store_metadata);
  key_store.set_wrapped_recovery_key(
      brillo::BlobToString(*wrapped_recovery_key));
  *key_store.mutable_wrapped_security_domain_key() =
      std::move(*wrapped_security_domain_key);
  return key_store;
}

}  // namespace cryptohome
