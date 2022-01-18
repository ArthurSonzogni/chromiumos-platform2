// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>
#include <variant>

#include <brillo/secure_blob.h>

#include "cryptohome/signature_sealing/structures_proto.h"
#include "cryptohome/tpm.h"

using brillo::BlobFromString;
using brillo::BlobToString;

namespace cryptohome {
namespace proto {

// We don't need to export these functions.
namespace {
SignatureSealedData_Tpm2PolicySignedData ToProto(
    const structure::Tpm2PolicySignedData& obj) {
  SignatureSealedData_Tpm2PolicySignedData result;
  result.set_public_key_spki_der(BlobToString(obj.public_key_spki_der));
  result.set_srk_wrapped_secret(BlobToString(obj.srk_wrapped_secret));
  if (obj.scheme.has_value()) {
    result.set_scheme(obj.scheme.value());
  }
  if (obj.hash_alg.has_value()) {
    result.set_hash_alg(obj.hash_alg.value());
  }

  // Special conversion for backwards-compatibility.
  // Note: The order of items added here is important, as it must match the
  // reading order in FromProto() and must never change due to backwards
  // compatibility.
  SignatureSealedData_PcrValue pcr_value;
  // Ignoring the exact PCR value, because we don't need it.
  pcr_value.set_pcr_index(kTpmSingleUserPCR);

  SignatureSealedData_Tpm2PcrRestriction restriction;
  restriction.set_policy_digest(BlobToString(obj.default_pcr_policy_digest));

  *restriction.add_pcr_values() = std::move(pcr_value);
  *result.add_pcr_restrictions() = std::move(restriction);

  pcr_value = SignatureSealedData_PcrValue();
  // Ignoring the exact PCR value, because we don't need it.
  pcr_value.set_pcr_index(kTpmSingleUserPCR);

  restriction = SignatureSealedData_Tpm2PcrRestriction();
  restriction.set_policy_digest(BlobToString(obj.extended_pcr_policy_digest));

  *restriction.add_pcr_values() = std::move(pcr_value);
  *result.add_pcr_restrictions() = std::move(restriction);

  return result;
}

structure::Tpm2PolicySignedData FromProto(
    const SignatureSealedData_Tpm2PolicySignedData& obj) {
  structure::Tpm2PolicySignedData result;
  result.public_key_spki_der = BlobFromString(obj.public_key_spki_der());
  result.srk_wrapped_secret = BlobFromString(obj.srk_wrapped_secret());
  if (obj.has_scheme()) {
    result.scheme = obj.scheme();
  }
  if (obj.has_hash_alg()) {
    result.hash_alg = obj.hash_alg();
  }

  // Special conversion for backwards-compatibility.
  if (obj.pcr_restrictions_size() == 2) {
    result.default_pcr_policy_digest =
        BlobFromString(obj.pcr_restrictions(0).policy_digest());
    result.extended_pcr_policy_digest =
        BlobFromString(obj.pcr_restrictions(1).policy_digest());
  } else {
    LOG(WARNING) << "Unknown PCR restrictions size from protobuf.";
  }

  return result;
}

SignatureSealedData_Tpm12CertifiedMigratableKeyData ToProto(
    const structure::Tpm12CertifiedMigratableKeyData& obj) {
  SignatureSealedData_Tpm12CertifiedMigratableKeyData result;
  result.set_public_key_spki_der(BlobToString(obj.public_key_spki_der));
  result.set_srk_wrapped_cmk(BlobToString(obj.srk_wrapped_cmk));
  result.set_cmk_pubkey(BlobToString(obj.cmk_pubkey));
  result.set_cmk_wrapped_auth_data(BlobToString(obj.cmk_wrapped_auth_data));

  // Special conversion for backwards-compatibility.
  // Note: The order of items added here is important, as it must match the
  // reading order in FromProto() and must never change due to backwards
  // compatibility.
  SignatureSealedData_PcrValue pcr_value;
  // Ignoring the exact PCR value, because we don't need it.
  pcr_value.set_pcr_index(kTpmSingleUserPCR);

  SignatureSealedData_Tpm12PcrBoundItem bound_item;
  bound_item.set_bound_secret(BlobToString(obj.default_pcr_bound_secret));

  *bound_item.add_pcr_values() = std::move(pcr_value);
  *result.add_pcr_bound_items() = std::move(bound_item);

  pcr_value = SignatureSealedData_PcrValue();
  // Ignoring the exact PCR value, because we don't need it.
  pcr_value.set_pcr_index(kTpmSingleUserPCR);

  bound_item = SignatureSealedData_Tpm12PcrBoundItem();
  bound_item.set_bound_secret(BlobToString(obj.extended_pcr_bound_secret));

  *bound_item.add_pcr_values() = std::move(pcr_value);
  *result.add_pcr_bound_items() = std::move(bound_item);

  return result;
}

structure::Tpm12CertifiedMigratableKeyData FromProto(
    const SignatureSealedData_Tpm12CertifiedMigratableKeyData& obj) {
  structure::Tpm12CertifiedMigratableKeyData result;
  result.public_key_spki_der = BlobFromString(obj.public_key_spki_der());
  result.srk_wrapped_cmk = BlobFromString(obj.srk_wrapped_cmk());
  result.cmk_pubkey = BlobFromString(obj.cmk_pubkey());
  result.cmk_wrapped_auth_data = BlobFromString(obj.cmk_wrapped_auth_data());

  // Special conversion for backwards-compatibility.
  if (obj.pcr_bound_items_size() == 2) {
    result.default_pcr_bound_secret =
        BlobFromString(obj.pcr_bound_items(0).bound_secret());
    result.extended_pcr_bound_secret =
        BlobFromString(obj.pcr_bound_items(1).bound_secret());
  } else {
    LOG(WARNING) << "Unknown PCR bound items size from protobuf.";
  }

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
  if (auto* data = std::get_if<structure::Tpm2PolicySignedData>(&obj)) {
    *result.mutable_tpm2_policy_signed_data() = ToProto(*data);
  } else if (auto* data =
                 std::get_if<structure::Tpm12CertifiedMigratableKeyData>(
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
  if (obj.salt_signature_algorithm.has_value()) {
    result.set_salt_signature_algorithm(
        ToProto(obj.salt_signature_algorithm.value()));
  }
  return result;
}

structure::SignatureChallengeInfo FromProto(
    const SerializedVaultKeyset_SignatureChallengeInfo& obj) {
  structure::SignatureChallengeInfo result;
  result.public_key_spki_der = BlobFromString(obj.public_key_spki_der());
  result.sealed_secret = FromProto(obj.sealed_secret());
  result.salt = BlobFromString(obj.salt());
  if (obj.has_salt_signature_algorithm()) {
    result.salt_signature_algorithm = FromProto(obj.salt_signature_algorithm());
  }
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
