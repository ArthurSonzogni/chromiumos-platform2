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

  // TPM policy digest for the TPM2_PolicyPCR command executed with default PCR
  // map.
  brillo::Blob default_pcr_policy_digest;

  // TPM policy digest for the TPM2_PolicyPCR command executed with extended PCR
  // map.
  brillo::Blob extended_pcr_policy_digest;
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

  // The secret blob, which is bound to the default PCR map.
  brillo::Blob default_pcr_bound_secret;

  // The secret blob, which is bound to the extended PCR map.
  brillo::Blob extended_pcr_bound_secret;
};

using SignatureSealedData = absl::variant<absl::monostate,
                                          Tpm2PolicySignedData,
                                          Tpm12CertifiedMigratableKeyData>;

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
