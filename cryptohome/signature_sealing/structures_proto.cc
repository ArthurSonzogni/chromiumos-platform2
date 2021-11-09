// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <absl/types/variant.h>
#include <brillo/secure_blob.h>

#include "cryptohome/signature_sealing/structures_proto.h"

using brillo::BlobFromString;
using brillo::BlobToString;

namespace cryptohome {
namespace proto {

// We don't need to export these functions.
namespace {
SignatureSealedData_PcrValue ToProto(const structure::PcrValue& obj) {
  SignatureSealedData_PcrValue result;
  result.set_pcr_index(obj.pcr_index);
  result.set_pcr_value(BlobToString(obj.pcr_value));
  return result;
}

structure::PcrValue FromProto(const SignatureSealedData_PcrValue& obj) {
  structure::PcrValue result;
  result.pcr_index = obj.pcr_index();
  result.pcr_value = BlobFromString(obj.pcr_value());
  return result;
}

SignatureSealedData_Tpm2PcrRestriction ToProto(
    const structure::Tpm2PcrRestriction& obj) {
  SignatureSealedData_Tpm2PcrRestriction result;
  for (const auto& content : obj.pcr_values)
    *result.add_pcr_values() = ToProto(content);
  result.set_policy_digest(BlobToString(obj.policy_digest));
  return result;
}

structure::Tpm2PcrRestriction FromProto(
    const SignatureSealedData_Tpm2PcrRestriction& obj) {
  structure::Tpm2PcrRestriction result;
  for (const auto& content : obj.pcr_values())
    result.pcr_values.push_back(FromProto(content));
  result.policy_digest = BlobFromString(obj.policy_digest());
  return result;
}

SignatureSealedData_Tpm2PolicySignedData ToProto(
    const structure::Tpm2PolicySignedData& obj) {
  SignatureSealedData_Tpm2PolicySignedData result;
  result.set_public_key_spki_der(BlobToString(obj.public_key_spki_der));
  result.set_srk_wrapped_secret(BlobToString(obj.srk_wrapped_secret));
  result.set_scheme(obj.scheme);
  result.set_hash_alg(obj.hash_alg);
  for (const auto& content : obj.pcr_restrictions)
    *result.add_pcr_restrictions() = ToProto(content);
  return result;
}

structure::Tpm2PolicySignedData FromProto(
    const SignatureSealedData_Tpm2PolicySignedData& obj) {
  structure::Tpm2PolicySignedData result;
  result.public_key_spki_der = BlobFromString(obj.public_key_spki_der());
  result.srk_wrapped_secret = BlobFromString(obj.srk_wrapped_secret());
  result.scheme = obj.scheme();
  result.hash_alg = obj.hash_alg();
  for (const auto& content : obj.pcr_restrictions())
    result.pcr_restrictions.push_back(FromProto(content));
  return result;
}

SignatureSealedData_Tpm12PcrBoundItem ToProto(
    const structure::Tpm12PcrBoundItem& obj) {
  SignatureSealedData_Tpm12PcrBoundItem result;
  for (const auto& content : obj.pcr_values)
    *result.add_pcr_values() = ToProto(content);
  result.set_bound_secret(BlobToString(obj.bound_secret));
  return result;
}

structure::Tpm12PcrBoundItem FromProto(
    const SignatureSealedData_Tpm12PcrBoundItem& obj) {
  structure::Tpm12PcrBoundItem result;
  for (const auto& content : obj.pcr_values())
    result.pcr_values.push_back(FromProto(content));
  result.bound_secret = BlobFromString(obj.bound_secret());
  return result;
}

SignatureSealedData_Tpm12CertifiedMigratableKeyData ToProto(
    const structure::Tpm12CertifiedMigratableKeyData& obj) {
  SignatureSealedData_Tpm12CertifiedMigratableKeyData result;
  result.set_public_key_spki_der(BlobToString(obj.public_key_spki_der));
  result.set_srk_wrapped_cmk(BlobToString(obj.srk_wrapped_cmk));
  result.set_cmk_pubkey(BlobToString(obj.cmk_pubkey));
  result.set_cmk_wrapped_auth_data(BlobToString(obj.cmk_wrapped_auth_data));
  for (const auto& content : obj.pcr_bound_items)
    *result.add_pcr_bound_items() = ToProto(content);
  return result;
}

structure::Tpm12CertifiedMigratableKeyData FromProto(
    const SignatureSealedData_Tpm12CertifiedMigratableKeyData& obj) {
  structure::Tpm12CertifiedMigratableKeyData result;
  result.public_key_spki_der = BlobFromString(obj.public_key_spki_der());
  result.srk_wrapped_cmk = BlobFromString(obj.srk_wrapped_cmk());
  result.cmk_pubkey = BlobFromString(obj.cmk_pubkey());
  result.cmk_wrapped_auth_data = BlobFromString(obj.cmk_wrapped_auth_data());
  for (const auto& content : obj.pcr_bound_items())
    result.pcr_bound_items.push_back(FromProto(content));
  return result;
}
}  // namespace

ChallengeSignatureAlgorithm ToProto(
    structure::ChallengeSignatureAlgorithm obj) {
  switch (obj) {
    case structure::ChallengeSignatureAlgorithm::kRsassaPkcs1V15Sha1:
      return ChallengeSignatureAlgorithm::CHALLENGE_RSASSA_PKCS1_V1_5_SHA1;
    case structure::ChallengeSignatureAlgorithm::kRsassaPkcs1V15Sha256:
      return ChallengeSignatureAlgorithm::CHALLENGE_RSASSA_PKCS1_V1_5_SHA256;
    case structure::ChallengeSignatureAlgorithm::kRsassaPkcs1V15Sha384:
      return ChallengeSignatureAlgorithm::CHALLENGE_RSASSA_PKCS1_V1_5_SHA384;
    case structure::ChallengeSignatureAlgorithm::kRsassaPkcs1V15Sha512:
      return ChallengeSignatureAlgorithm::CHALLENGE_RSASSA_PKCS1_V1_5_SHA512;
  }
}

structure::ChallengeSignatureAlgorithm FromProto(
    ChallengeSignatureAlgorithm obj) {
  switch (obj) {
    case ChallengeSignatureAlgorithm::CHALLENGE_RSASSA_PKCS1_V1_5_SHA1:
      return structure::ChallengeSignatureAlgorithm::kRsassaPkcs1V15Sha1;
    case ChallengeSignatureAlgorithm::CHALLENGE_RSASSA_PKCS1_V1_5_SHA256:
      return structure::ChallengeSignatureAlgorithm::kRsassaPkcs1V15Sha256;
    case ChallengeSignatureAlgorithm::CHALLENGE_RSASSA_PKCS1_V1_5_SHA384:
      return structure::ChallengeSignatureAlgorithm::kRsassaPkcs1V15Sha384;
    case ChallengeSignatureAlgorithm::CHALLENGE_RSASSA_PKCS1_V1_5_SHA512:
      return structure::ChallengeSignatureAlgorithm::kRsassaPkcs1V15Sha512;
  }
}

SignatureSealedData ToProto(const structure::SignatureSealedData& obj) {
  SignatureSealedData result;
  if (auto* data = absl::get_if<structure::Tpm2PolicySignedData>(&obj)) {
    *result.mutable_tpm2_policy_signed_data() = ToProto(*data);
  } else if (auto* data =
                 absl::get_if<structure::Tpm12CertifiedMigratableKeyData>(
                     &obj)) {
    *result.mutable_tpm12_certified_migratable_key_data() = ToProto(*data);
  } else {
    NOTREACHED() << "Unknown signature sealed data type.";
  }
  return result;
}

structure::SignatureSealedData FromProto(const SignatureSealedData& obj) {
  if (obj.has_tpm2_policy_signed_data())
    return FromProto(obj.tpm2_policy_signed_data());
  else if (obj.has_tpm12_certified_migratable_key_data())
    return FromProto(obj.tpm12_certified_migratable_key_data());

  LOG(WARNING) << "Unknown signature sealed data type from protobuf.";
  // Return with the default constructor.
  return {};
}

SerializedVaultKeyset_SignatureChallengeInfo ToProto(
    const structure::SignatureChallengeInfo& obj) {
  SerializedVaultKeyset_SignatureChallengeInfo result;
  result.set_public_key_spki_der(BlobToString(obj.public_key_spki_der));
  *result.mutable_sealed_secret() = ToProto(obj.sealed_secret);
  result.set_salt(BlobToString(obj.salt));
  result.set_salt_signature_algorithm(ToProto(obj.salt_signature_algorithm));
  return result;
}

structure::SignatureChallengeInfo FromProto(
    const SerializedVaultKeyset_SignatureChallengeInfo& obj) {
  structure::SignatureChallengeInfo result;
  result.public_key_spki_der = BlobFromString(obj.public_key_spki_der());
  result.sealed_secret = FromProto(obj.sealed_secret());
  result.salt = BlobFromString(obj.salt());
  result.salt_signature_algorithm = FromProto(obj.salt_signature_algorithm());
  return result;
}

ChallengePublicKeyInfo ToProto(const structure::ChallengePublicKeyInfo& obj) {
  ChallengePublicKeyInfo result;
  result.set_public_key_spki_der(BlobToString(obj.public_key_spki_der));
  for (const auto& content : obj.signature_algorithm)
    result.add_signature_algorithm(ToProto(content));
  return result;
}

structure::ChallengePublicKeyInfo FromProto(const ChallengePublicKeyInfo& obj) {
  structure::ChallengePublicKeyInfo result;
  result.public_key_spki_der = BlobFromString(obj.public_key_spki_der());
  for (const auto& content : obj.signature_algorithm()) {
    result.signature_algorithm.push_back(
        FromProto(ChallengeSignatureAlgorithm{content}));
  }
  return result;
}

}  // namespace proto
}  // namespace cryptohome
