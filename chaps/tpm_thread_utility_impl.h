// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHAPS_TPM_THREAD_UTILITY_IMPL_H_
#define CHAPS_TPM_THREAD_UTILITY_IMPL_H_

#include "chaps/async_tpm_utility.h"
#include "chaps/tpm_utility.h"

#include <memory>
#include <string>

#include <base/task/single_thread_task_runner.h>
#include <base/threading/thread.h>

namespace chaps {

// TPMThreadUtilityImpl will execute all TPM operations on a standalong thread.
// All member functions are thread-safe, and all callbacks will post back to the
// origin task runner.
class TPMThreadUtilityImpl : public AsyncTPMUtility {
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
  bool GenerateRandom(int num_bytes, std::string* random_data) override;
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
  void UnloadKeysForSlot(int slot) override;
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

  // These functions called the correspondinf functions asynchronously.
  // Note: The callback would be executed on the caller thread, so the caller
  // needs to create an event loop before calling these functions.
  void GenerateRandomAsync(int num_bytes,
                           GenerateRandomCallback callback) override;
  void UnloadKeysForSlotAsync(int slot,
                              UnloadKeysForSlotCallback callback) override;
  void SealDataAsync(const std::string& unsealed_data,
                     const brillo::SecureBlob& auth_value,
                     SealDataCallback callback) override;
  void UnsealDataAsync(const std::string& key_blob,
                       const std::string& encrypted_data,
                       const brillo::SecureBlob& auth_value,
                       UnsealDataCallback callback) override;

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
