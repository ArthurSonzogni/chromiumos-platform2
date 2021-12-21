// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHAPS_TPM_THREAD_UTILITY_IMPL_H_
#define CHAPS_TPM_THREAD_UTILITY_IMPL_H_

#include "chaps/tpm_utility.h"

#include <memory>
#include <string>

#include <base/single_thread_task_runner.h>
#include <base/threading/thread.h>

namespace chaps {

// TPMThreadUtilityImpl will execute all TPM operations on a standalong thread.
// All member functions are thread-safe, and all callbacks will post back to the
// origin task runner.
class TPMThreadUtilityImpl : public TPMUtility {
 public:
  explicit TPMThreadUtilityImpl(std::unique_ptr<TPMUtility> inner_tpm);
  TPMThreadUtilityImpl(const TPMThreadUtilityImpl&) = delete;
  TPMThreadUtilityImpl& operator=(const TPMThreadUtilityImpl&) = delete;

  ~TPMThreadUtilityImpl() override;

  // These simple functions don't need to be called on the TPM thread.
  size_t MinRSAKeyBits() override;
  size_t MaxRSAKeyBits() override;
  TPMVersion GetTPMVersion() override;

  // These functions need to be called on the TPM thread.
  bool Init() override;
  bool IsTPMAvailable() override;
  bool Authenticate(const brillo::SecureBlob& auth_data,
                    const std::string& auth_key_blob,
                    const std::string& encrypted_root_key,
                    brillo::SecureBlob* root_key) override;
  bool ChangeAuthData(const brillo::SecureBlob& old_auth_data,
                      const brillo::SecureBlob& new_auth_data,
                      const std::string& old_auth_key_blob,
                      std::string* new_auth_key_blob) override;
  bool GenerateRandom(int num_bytes, std::string* random_data) override;
  bool StirRandom(const std::string& entropy_data) override;
  bool GenerateRSAKey(int slot,
                      int modulus_bits,
                      const std::string& public_exponent,
                      const brillo::SecureBlob& auth_data,
                      std::string* key_blob,
                      int* key_handle) override;
  bool GetRSAPublicKey(int key_handle,
                       std::string* public_exponent,
                       std::string* modulus) override;
  bool IsECCurveSupported(int curve_nid) override;
  bool GenerateECCKey(int slot,
                      int nid,
                      const brillo::SecureBlob& auth_data,
                      std::string* key_blob,
                      int* key_handle) override;
  bool GetECCPublicKey(int key_handle, std::string* public_point) override;
  bool WrapRSAKey(int slot,
                  const std::string& public_exponent,
                  const std::string& modulus,
                  const std::string& prime_factor,
                  const brillo::SecureBlob& auth_data,
                  std::string* key_blob,
                  int* key_handle) override;
  bool WrapECCKey(int slot,
                  int curve_nid,
                  const std::string& public_point_x,
                  const std::string& public_point_y,
                  const std::string& private_value,
                  const brillo::SecureBlob& auth_data,
                  std::string* key_blob,
                  int* key_handle) override;
  bool LoadKey(int slot,
               const std::string& key_blob,
               const brillo::SecureBlob& auth_data,
               int* key_handle) override;
  bool LoadKeyWithParent(int slot,
                         const std::string& key_blob,
                         const brillo::SecureBlob& auth_data,
                         int parent_key_handle,
                         int* key_handle) override;
  void UnloadKeysForSlot(int slot) override;
  bool Bind(int key_handle,
            const std::string& input,
            std::string* output) override;
  bool Unbind(int key_handle,
              const std::string& input,
              std::string* output) override;
  bool Sign(int key_handle,
            CK_MECHANISM_TYPE signing_mechanism,
            const std::string& mechanism_parameter,
            const std::string& input,
            std::string* signature) override;
  bool IsSRKReady() override;
  bool SealData(const std::string& unsealed_data,
                const brillo::SecureBlob& auth_value,
                std::string* key_blob,
                std::string* encrypted_data) override;
  bool UnsealData(const std::string& key_blob,
                  const std::string& encrypted_data,
                  const brillo::SecureBlob& auth_value,
                  brillo::SecureBlob* unsealed_data) override;

 private:
  // base::Thread subclass so we can implement CleanUp.
  class TPMThread : public base::Thread {
   public:
    explicit TPMThread(const std::string& name, TPMThreadUtilityImpl* uda);
    TPMThread(const TPMThread&) = delete;
    TPMThread& operator=(const TPMThread&) = delete;
    ~TPMThread() override;

   private:
    void CleanUp() override;
    TPMThreadUtilityImpl* const utility_;
  };

  template <typename MethodType, typename ResultType, typename... Args>
  void SendRequestAndWaitResult(const MethodType& method,
                                ResultType* result,
                                Args&&... args);
  template <typename MethodType, typename... Args>
  void SendRequestAndWait(const MethodType& method, Args&&... args);

  std::unique_ptr<TPMUtility> inner_tpm_;
  std::unique_ptr<TPMThread> tpm_thread_;
  scoped_refptr<base::TaskRunner> task_runner_;
};

}  // namespace chaps

#endif  // CHAPS_TPM_THREAD_UTILITY_IMPL_H_
