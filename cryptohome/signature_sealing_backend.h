// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_SIGNATURE_SEALING_BACKEND_H_
#define CRYPTOHOME_SIGNATURE_SEALING_BACKEND_H_

#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <brillo/secure_blob.h>
#include <libhwsec/error/tpm_error.h>

#include "cryptohome/key.pb.h"
#include "cryptohome/signature_sealed_data.pb.h"
#include "cryptohome/signature_sealing/structures.h"

namespace cryptohome {

// Interface for performing signature-sealing operations using the TPM.
// Implementations of this interface are exposed by the Tpm class subclasses.
class SignatureSealingBackend {
 public:
  // Interface for a session of unsealing the sealed secret.
  // Instances can be obtained via
  // SignatureSealingBackend::CreateUnsealingSession().
  //
  // Unless the implementation documents otherwise, all methods of this class
  // have to be called from a single thread - the thread on which
  // SignatureSealingBackend::CreateUnsealingSession() was called.
  class UnsealingSession {
   public:
    virtual ~UnsealingSession() = default;

    // Returns the algorithm to be used for signing the challenge value.
    virtual structure::ChallengeSignatureAlgorithm GetChallengeAlgorithm() = 0;

    // Returns the challenge value to be signed.
    virtual brillo::Blob GetChallengeValue() = 0;

    // Attempts to complete the unsealing, given the signature of the challenge
    // value.
    //
    // Should normally be called only once.
    //
    // Parameters
    //   signed_challenge_value - Signature of the blob returned by
    //                            GetChallengeValue() using the algorithm as
    //                            returned by GetChallengeAlgorithm().
    //   unsealed_value - The unsealed value, if the function returned true.
    virtual hwsec::StatusChain<hwsec::TPMErrorBase> Unseal(
        const brillo::Blob& signed_challenge_value,
        brillo::SecureBlob* unsealed_value) = 0;
  };

  virtual ~SignatureSealingBackend() = default;

  // Creates a random secret and seals it with the specified key, so that
  // unsealing is gated on providing a valid signature for the challenge.
  //
  // Parameters
  //   public_key_spki_der - The DER-encoded Subject Public Key Info of the key
  //                         using which the secret should be sealed.
  //   key_algorithms - The list of signature algorithms supported by the key.
  //                    Listed in the order of preference (starting from the
  //                    most preferred); however, the implementation is
  //                    permitted to ignore this order.
  //   default_pcr_map - The default PCR values map; the created secret will be
  //                     protected in a way that decrypting it back is possible
  //                     iff at least one of the PCR value maps is satisfied.
  //   extended_pcr_map - The extend PCR values map; the created secret will be
  //                      protected in a way that decrypting it back is possible
  //                      iff at least one of the PCR value maps is satisfied.
  //   delegate_blob - The blob for the owner delegation.
  //   delegate_secret - The delegate secret for the delegate blob.
  //   secret_value - The created secret value.
  //   sealed_secret_data - Securely sealed representation of the secret value.
  virtual hwsec::StatusChain<hwsec::TPMErrorBase> CreateSealedSecret(
      const brillo::Blob& public_key_spki_der,
      const std::vector<structure::ChallengeSignatureAlgorithm>& key_algorithms,
      const std::map<uint32_t, brillo::Blob>& default_pcr_map,
      const std::map<uint32_t, brillo::Blob>& extended_pcr_map,
      const brillo::Blob& delegate_blob,
      const brillo::Blob& delegate_secret,
      brillo::SecureBlob* secret_value,
      structure::SignatureSealedData* sealed_secret_data) = 0;

  // Initiates a session for unsealing the passed sealed data.
  // Note: the implementation may impose restrictions on the number of unsealing
  // sessions that are allowed to coexist simultaneously.
  //
  // Parameters
  //   sealed_secret_data - The sealed value, as created by
  //                        CreateSealedSecret().
  //   public_key_spki_der - The DER-encoded Subject Public Key Info of the key
  //                         to be challenged for unsealing.
  //   key_algorithms - The list of signature algorithms supported by the key.
  //                    Listed in the order of preference (starting from the
  //                    most preferred); however, the implementation is
  //                    permitted to ignore this order.
  //   pcr_set - The PCR values set; the set would be used to unseal the secret.
  //   delegate_blob - The blob for the owner delegation.
  //   delegate_secret - The delegate secret for the delegate blob.
  //   locked_to_single_user - Should use extended PCR to unseal or not.
  virtual hwsec::StatusChain<hwsec::TPMErrorBase> CreateUnsealingSession(
      const structure::SignatureSealedData& sealed_secret_data,
      const brillo::Blob& public_key_spki_der,
      const std::vector<structure::ChallengeSignatureAlgorithm>& key_algorithms,
      const std::set<uint32_t>& pcr_set,
      const brillo::Blob& delegate_blob,
      const brillo::Blob& delegate_secret,
      bool locked_to_single_user,
      std::unique_ptr<UnsealingSession>* unsealing_session) = 0;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_SIGNATURE_SEALING_BACKEND_H_
