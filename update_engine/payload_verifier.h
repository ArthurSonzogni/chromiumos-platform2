// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_PAYLOAD_VERIFIER_H_
#define UPDATE_ENGINE_PAYLOAD_VERIFIER_H_

#include <string>
#include <vector>

#include <base/basictypes.h>
#include "update_engine/update_metadata.pb.h"

// This class encapsulates methods used for payload signature verification.
// See payload_generator/payload_signer.h for payload signing.

namespace chromeos_update_engine {

extern const uint32_t kSignatureMessageOriginalVersion;
extern const uint32_t kSignatureMessageCurrentVersion;

class PayloadVerifier {
 public:
  // Returns false if the payload signature can't be verified. Returns true
  // otherwise and sets |out_hash| to the signed payload hash.
  static bool VerifySignature(const std::vector<char>& signature_blob,
                              const std::string& public_key_path,
                              std::vector<char>* out_hash_data);

  // Interprets signature_blob as a protocol buffer containing the Signatures
  // message and decrypts the signature data using the public_key_path and
  // stores the resultant raw hash data in out_hash_data. Returns true if
  // everything is successful. False otherwise. It also takes the client_version
  // and interprets the signature blob according to that version.
  static bool VerifySignatureBlob(const std::vector<char>& signature_blob,
                                  const std::string& public_key_path,
                                  uint32_t client_version,
                                  std::vector<char>* out_hash_data);

  // Decrypts sig_data with the given public_key_path and populates
  // out_hash_data with the decoded raw hash. Returns true if successful,
  // false otherwise.
  static bool GetRawHashFromSignature(const std::vector<char>& sig_data,
                                      const std::string& public_key_path,
                                      std::vector<char>* out_hash_data);

  // Returns true if the payload in |payload_path| is signed and its hash can be
  // verified using the public key in |public_key_path| with the signature
  // of a given version in the signature blob. Returns false otherwise.
  static bool VerifySignedPayload(const std::string& payload_path,
                                  const std::string& public_key_path,
                                  uint32_t client_key_check_version);

  // Pads a SHA256 hash so that it may be encrypted/signed with RSA2048
  // using the PKCS#1 v1.5 scheme.
  // hash should be a pointer to vector of exactly 256 bits. The vector
  // will be modified in place and will result in having a length of
  // 2048 bits. Returns true on success, false otherwise.
  static bool PadRSA2048SHA256Hash(std::vector<char>* hash);

  // Reads the payload from the given |payload_path| into the |out_payload|
  // vector. It also parses the manifest protobuf in the payload and returns it
  // in |out_manifest| along with the size of the entire metadata in
  // |out_metadata_size|.
  static bool LoadPayload(const std::string& payload_path,
                          std::vector<char>* out_payload,
                          DeltaArchiveManifest* out_manifest,
                          uint64_t* out_metadata_size);

 private:
  // This should never be constructed
  DISALLOW_IMPLICIT_CONSTRUCTORS(PayloadVerifier);
};

}  // namespace chromeos_update_engine

#endif  // UPDATE_ENGINE_PAYLOAD_VERIFIER_H_
