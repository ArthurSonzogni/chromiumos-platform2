// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHAPS_TPM_UTILITY_STUB_H_
#define CHAPS_TPM_UTILITY_STUB_H_

#include <string>

#include <brillo/secure_blob.h>

#include "chaps/tpm_utility.h"

namespace chaps {

class TPMUtilityStub : public TPMUtility {
 public:
  TPMUtilityStub() = default;
  ~TPMUtilityStub() override = default;

  size_t MinRSAKeyBits() override { return 0; }

  size_t MaxRSAKeyBits() override { return 0; }

  bool Init() override { return false; }

  bool IsTPMAvailable() override { return false; }

  TPMVersion GetTPMVersion() override { return TPMVersion::TPM1_2; }

  bool Authenticate(const brillo::SecureBlob& auth_data,
                    const std::string& auth_key_blob,
                    const std::string& encrypted_root_key,
                    brillo::SecureBlob* root_key) override {
    return false;
  }

  bool ChangeAuthData(const brillo::SecureBlob& old_auth_data,
                      const brillo::SecureBlob& new_auth_data,
                      const std::string& old_auth_key_blob,
                      std::string* new_auth_key_blob) override {
    return false;
  }

  bool GenerateRandom(int num_bytes, std::string* random_data) override {
    return false;
  }

  bool StirRandom(const std::string& entropy_data) override { return false; }

  bool GenerateRSAKey(int slot,
                      int modulus_bits,
                      const std::string& public_exponent,
                      const brillo::SecureBlob& auth_data,
                      std::string* key_blob,
                      int* key_handle) override {
    return false;
  }

  bool GetRSAPublicKey(int key_handle,
                       std::string* public_exponent,
                       std::string* modulus) override {
    return false;
  }

  bool IsECCurveSupported(int curve_nid) override { return false; }

  bool GenerateECCKey(int slot,
                      int nid,
                      const brillo::SecureBlob& auth_data,
                      std::string* key_blob,
                      int* key_handle) override {
    return false;
  }

  bool GetECCPublicKey(int key_handle, std::string* public_point) override {
    return false;
  }

  bool WrapRSAKey(int slot,
                  const std::string& public_exponent,
                  const std::string& modulus,
                  const std::string& prime_factor,
                  const brillo::SecureBlob& auth_data,
                  std::string* key_blob,
                  int* key_handle) override {
    return false;
  }

  bool WrapECCKey(int slot,
                  int curve_nid,
                  const std::string& public_point_x,
                  const std::string& public_point_y,
                  const std::string& private_value,
                  const brillo::SecureBlob& auth_data,
                  std::string* key_blob,
                  int* key_handle) override {
    return false;
  }

  bool LoadKey(int slot,
               const std::string& key_blob,
               const brillo::SecureBlob& auth_data,
               int* key_handle) override {
    return false;
  }

  bool LoadKeyWithParent(int slot,
                         const std::string& key_blob,
                         const brillo::SecureBlob& auth_data,
                         int parent_key_handle,
                         int* key_handle) override {
    return false;
  }

  void UnloadKeysForSlot(int slot) override {}

  bool Bind(int key_handle,
            const std::string& input,
            std::string* output) override {
    return false;
  }

  bool Unbind(int key_handle,
              const std::string& input,
              std::string* output) override {
    return false;
  }

  bool Sign(int key_handle,
            CK_MECHANISM_TYPE signing_mechanism,
            const std::string& mechanism_parameter,
            const std::string& input,
            std::string* signature) override {
    return false;
  }

  bool IsSRKReady() override { return false; }

  bool SealData(const std::string& unsealed_data,
                const brillo::SecureBlob& auth_value,
                std::string* key_blob,
                std::string* encrypted_data) override {
    return false;
  }

  bool UnsealData(const std::string& key_blob,
                  const std::string& encrypted_data,
                  const brillo::SecureBlob& auth_value,
                  brillo::SecureBlob* unsealed_data) override {
    return false;
  }
};

}  // namespace chaps

#endif  // CHAPS_TPM_UTILITY_STUB_H_
