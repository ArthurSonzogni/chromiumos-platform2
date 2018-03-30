//
// Copyright (C) 2015 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#ifndef ATTESTATION_COMMON_TPM_UTILITY_H_
#define ATTESTATION_COMMON_TPM_UTILITY_H_

#include <string>

#include "attestation/common/interface.pb.h"

namespace attestation {

// A class which provides helpers for TPM-related tasks.
class TpmUtility {
 public:
  virtual ~TpmUtility() = default;

  // Override to perform initialization work. This must be called successfully
  // before calling any other methods.
  virtual bool Initialize() = 0;

  // Returns the TPM version managed by this instance.
  virtual TpmVersion GetVersion() = 0;

  // Returns true iff the TPM is enabled, owned, and ready for attestation.
  virtual bool IsTpmReady() = 0;

  // Activates an attestation identity key for TPM 1.2. Effectively this
  // decrypts a certificate or some other type of credential with the
  // endorsement key. The |delegate_blob| and |delegate_secret| must be
  // authorized to activate with owner privilege. The |identity_key_blob| is the
  // key to which the credential is bound. The |asym_ca_contents| and
  // |sym_ca_attestation| parameters are encrypted TPM structures, typically
  // created by a CA (TPM_ASYM_CA_CONTENTS and TPM_SYM_CA_ATTESTATION
  // respectively). On success returns true and populates the decrypted
  // |credential|.
  virtual bool ActivateIdentity(const std::string& delegate_blob,
                                const std::string& delegate_secret,
                                const std::string& identity_key_blob,
                                const std::string& asym_ca_contents,
                                const std::string& sym_ca_attestation,
                                std::string* credential) = 0;

  // Activates an attestation identity key for TPM 2.0. The type of both the
  // endorsement key and the identity key is specified by |key_type|. The
  // |identity_key_blob| is as output by CreateRestrictedKey(). The
  // |encrypted_seed|, |credential_mac|, and |wrapped_credential| are provided
  // by the Attestation CA via an EncryptedIdentityCredential protobuf. Take
  // note that the |wrapped_credential| is not the wrapped certificate itself
  // but a shorter value which is used to derive
  virtual bool ActivateIdentityForTpm2(
      KeyType key_type,
      const std::string& identity_key_blob,
      const std::string& encrypted_seed,
      const std::string& credential_mac,
      const std::string& wrapped_credential,
      std::string* credential) = 0;

  // Generates and certifies a non-migratable key in the TPM. The new key will
  // correspond to |key_type| and |key_usage|. The parent key will be the
  // storage root key. The new key will be certified with the attestation
  // identity key represented by |identity_key_blob|. The |external_data| will
  // be included in the |key_info|. On success, returns true and populates
  // |public_key_tpm_format| with the public key of |key_blob| in TPM_PUBKEY
  // format, |key_info| with the TPM_CERTIFY_INFO that was signed, and |proof|
  // with the signature of |key_info| by the identity key.
  virtual bool CreateCertifiedKey(KeyType key_type,
                                  KeyUsage key_usage,
                                  const std::string& identity_key_blob,
                                  const std::string& external_data,
                                  std::string* key_blob,
                                  std::string* public_key,
                                  std::string* public_key_tpm_format,
                                  std::string* key_info,
                                  std::string* proof) = 0;

  // Seals |data| to the current value of PCR0 with the SRK and produces the
  // |sealed_data|. Returns true on success.
  virtual bool SealToPCR0(const std::string& data,
                          std::string* sealed_data) = 0;

  // Unseals |sealed_data| previously sealed with the SRK and produces the
  // unsealed |data|. Returns true on success.
  virtual bool Unseal(const std::string& sealed_data, std::string* data) = 0;

  // Reads an endorsement public key from the TPM and provides it as a DER
  // encoded PKCS #1 RSAPublicKey.
  virtual bool GetEndorsementPublicKey(KeyType key_type,
                                       std::string* public_key) = 0;

  // Reads an endorsement certificate from the TPM.
  virtual bool GetEndorsementCertificate(KeyType key_type,
                                         std::string* certificate) = 0;

  // Unbinds |bound_data| with the key loaded from |key_blob| by decrypting
  // using the TPM_ES_RSAESOAEP_SHA1_MGF1 scheme. The input must be in the
  // format of a TPM_BOUND_DATA structure. On success returns true and provides
  // the decrypted |data|.
  virtual bool Unbind(const std::string& key_blob,
                      const std::string& bound_data,
                      std::string* data) = 0;

  // Signs |data_to_sign| with the key loaded from |key_blob| using the
  // TPM_SS_RSASSAPKCS1v15_DER scheme with SHA-256. On success returns true and
  // provides the |signature|.
  virtual bool Sign(const std::string& key_blob,
                    const std::string& data_to_sign,
                    std::string* signature) = 0;

  // Creates a restricted key of |key_type| for |key_usage|. The |public_key| is
  // a serialized TPMT_PUBLIC and |private_key_blob| is an opaque blob which
  // only the TPM is able to unwrap.
  virtual bool CreateRestrictedKey(KeyType key_type,
                                   KeyUsage key_usage,
                                   std::string* public_key,
                                   std::string* private_key_blob) = 0;

  // Quotes a PCR specified by |pcr_index|. The |key_blob| must be a restricted
  // signing key. On success returns true and populates:
  //   |quoted_pcr_value| - The value of the register at the time it was quoted.
  //   |quoted_data| - The exact serialized data that was signed.
  //   |quote| - The signature.
  virtual bool QuotePCR(int pcr_index,
                        const std::string& key_blob,
                        std::string* quoted_pcr_value,
                        std::string* quoted_data,
                        std::string* quote) = 0;

  // Checks if the provided |quote| is a valid quote for a single PCR specified
  // by |pcr_index|.
  virtual bool IsQuoteForPCR(const std::string& quote, int pcr_index) const = 0;

  // Reads a PCR specified by |pcr_index|. On success returns true and
  // populates |_pcr_value|.
  virtual bool ReadPCR(int pcr_index,
                       std::string* pcr_value) const = 0;

  // Convert a |tpm_public_key_object|, that is, a serialized TPM_PUBKEY for
  // TPM 1.2 or serialized TPMT_PUBLIC for TPM 2.0, to a DER encoded PKCS #1
  // RSAPublicKey.
  virtual bool GetRSAPublicKeyFromTpmPublicKey(
      const std::string& tpm_public_key_object,
      std::string* public_key_der) = 0;

  // Signals to remove Attestation dependency on owner password.
  // Returns true if the dependency was removed this time, or it already has
  // been removed earlier; false otherwise.
  virtual bool RemoveOwnerDependency() = 0;

  // Reads an endorsement public key from the TPM and extracts the modulus in
  // |ekm|.
  virtual bool GetEndorsementPublicKeyModulus(KeyType key_type,
                                              std::string* ekm) = 0;
};

}  // namespace attestation

#endif  // ATTESTATION_COMMON_TPM_UTILITY_H_
