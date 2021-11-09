// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_SIGNATURE_SEALING_STRUCTURES_H_
#define CRYPTOHOME_SIGNATURE_SEALING_STRUCTURES_H_

#include <vector>

#include <absl/types/variant.h>
#include <brillo/secure_blob.h>

namespace cryptohome {
namespace structure {

// Cryptographic signature algorithm type for challenge requests. Used with
// challenge-response cryptohome keys.
enum class ChallengeSignatureAlgorithm {
  kRsassaPkcs1V15Sha1 = 1,
  kRsassaPkcs1V15Sha256 = 2,
  kRsassaPkcs1V15Sha384 = 3,
  kRsassaPkcs1V15Sha512 = 4,
};

// Index and value of a TPM Platform Configuration Register (PCR).
struct PcrValue {
  uint32_t pcr_index = 0;
  brillo::Blob pcr_value;
};

// Information about a single set of PCR restrictions for TPM 2.0.
struct Tpm2PcrRestriction {
  // List of PCR values that must be all satisfied for this restriction.
  std::vector<PcrValue> pcr_values;

  // TPM policy digest for the TPM2_PolicyPCR command executed with the PCR
  // values specified by |pcr_values|.
  brillo::Blob policy_digest;
};

// Data for the TPM 2.0 method based on the "TPM2_PolicySigned" feature.
struct Tpm2PolicySignedData {
  // DER-encoded blob of the X.509 Subject Public Key Info of the key that
  // should be used for unsealing.
  brillo::Blob public_key_spki_der;

  // The secret blob, wrapped by the TPM's Storage Root Key.
  brillo::Blob srk_wrapped_secret;

  // The signature scheme (TPM_ALG_ID) that should be used for unsealing.
  int32_t scheme = 0;

  // The signature hash algorithm (TPM_ALG_ID) that should be used for
  // unsealing.
  int32_t hash_alg = 0;

  // Multiple alternative sets of PCR restrictions that are applied to the
  // wrapped secret. For unsealing, it's enough to satisfy only one of those
  // restrictions.
  // Note that the order of items here is important: it defines the order of
  // arguments when building the TPM policy digest.
  std::vector<Tpm2PcrRestriction> pcr_restrictions;
};

// TPM 1.2 data that is bound to the specific set of PCRs.
struct Tpm12PcrBoundItem {
  // Set of PCRs to which the secret blob is bound.
  std::vector<PcrValue> pcr_values;

  // The secret blob, which is bound to the PCR values specified by
  // |pcr_values| and with the AuthData value that is stored encrypted in
  // |cmk_wrapped_auth_data|.
  brillo::Blob bound_secret;
};

// Data for the TPM 1.2 method based on the "Certified Migratable Key"
// functionality.
struct Tpm12CertifiedMigratableKeyData {
  // DER-encoded blob of the X.509 Subject Public Key Info of the key that
  // should be used for unsealing.
  brillo::Blob public_key_spki_der;

  // The blob of the Certified Migratable Key wrapped by the TPM's Storage
  // Root Key.
  brillo::Blob srk_wrapped_cmk;

  // The TPM_PUBKEY blob of the Certified Migratable Key.
  brillo::Blob cmk_pubkey;

  // The AuthData blob encrypted by the CMK using the RSAES-OAEP MGF1
  // algorithm.
  brillo::Blob cmk_wrapped_auth_data;

  // Multiple alternative representations of the secret data, where each
  // representation is bound to its specific set of PCRs and to the AuthData
  // value that is stored encrypted in |cmk_wrapped_auth_data|.
  std::vector<Tpm12PcrBoundItem> pcr_bound_items;
};

using SignatureSealedData =
    absl::variant<Tpm2PolicySignedData, Tpm12CertifiedMigratableKeyData>;

// Fields specific to the challenge-response protection.
// The Scrypt KDF passphrase, used for the protection of the keyset, is
// defined as a concatenation of two values:
// * The first is the blob which is sealed in |sealed_secret|.
// * The second is the deterministic signature of |salt| using the
//   |salt_signature_algorithm| algorithm.
// The cryptographic key specified in |public_key_spki_der| is used for both.
struct SignatureChallengeInfo {
  // DER-encoded blob of the X.509 Subject Public Key Info of the key to be
  // challenged in order to obtain the KDF passphrase for decrypting the vault
  // keyset.
  brillo::Blob public_key_spki_der;
  // Container with the secret data which is sealed using the TPM in a way
  // that the process of its unsealing involves signature challenges against
  // the specified key. This secret data is one of the sources for building
  // the KDF passphrase.
  SignatureSealedData sealed_secret;
  // Salt whose signature is another source for building the KDF passphrase.
  brillo::Blob salt;
  // Signature algorithm to be used for signing |salt|.
  // NOTE: the signature algorithm has to be deterministic (that is, always
  // produce the same output for the same input).
  ChallengeSignatureAlgorithm salt_signature_algorithm;
};

// Description of a public key of an asymmetric cryptographic key. Used with
// challenge-response cryptohome keys.
struct ChallengePublicKeyInfo {
  // DER-encoded blob of the X.509 Subject Public Key Info.
  brillo::Blob public_key_spki_der;
  // Supported signature algorithms, in the order of preference (starting from
  // the most preferred). Absence of this field denotes that the key cannot be
  // used for signing.
  std::vector<ChallengeSignatureAlgorithm> signature_algorithm;
};

}  // namespace structure
}  // namespace cryptohome

#endif  // CRYPTOHOME_SIGNATURE_SEALING_STRUCTURES_H_
