// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <utility>

#include "chaps/tpm_thread_utility_impl.h"

#include <base/bind.h>
#include <base/bind_post_task.h>
#include <base/synchronization/waitable_event.h>
#include <base/threading/thread.h>
#include <base/threading/thread_task_runner_handle.h>

namespace {
constexpr char kTPMThreadName[] = "tpm_thread";
}

namespace chaps {

TPMThreadUtilityImpl::TPMThread::TPMThread(const std::string& name,
                                           TPMThreadUtilityImpl* utility)
    : base::Thread(name), utility_(utility) {
  CHECK(utility_);
}

TPMThreadUtilityImpl::TPMThread::~TPMThread() {
  Stop();
}

void TPMThreadUtilityImpl::TPMThread::CleanUp() {
  utility_->inner_tpm_.reset();
}

TPMThreadUtilityImpl::TPMThreadUtilityImpl(
    std::unique_ptr<TPMUtility> inner_tpm)
    : inner_tpm_(std::move(inner_tpm)),
      tpm_thread_(std::make_unique<TPMThread>(kTPMThreadName, this)) {
  base::Thread::Options options;
  options.message_pump_type = base::MessagePumpType::IO;
  tpm_thread_->StartWithOptions(std::move(options));
  task_runner_ = tpm_thread_->task_runner();
}

TPMThreadUtilityImpl::~TPMThreadUtilityImpl() {
  // The |inner_tpm_| would be destructed on |tpm_thread_|.
  tpm_thread_.reset();
}

template <typename MethodType, typename ResultType, typename... Args>
void TPMThreadUtilityImpl::SendRequestAndWaitResult(const MethodType& method,
                                                    ResultType* result,
                                                    Args&&... args) {
  base::WaitableEvent event(base::WaitableEvent::ResetPolicy::MANUAL,
                            base::WaitableEvent::InitialState::NOT_SIGNALED);
  base::OnceClosure task =
      base::BindOnce(method, base::Unretained(inner_tpm_.get()),
                     std::forward<Args>(args)...)
          .Then(base::BindOnce(
              [](ResultType* result, ResultType value) {
                *result = std::move(value);
              },
              result))
          .Then(base::BindOnce(&base::WaitableEvent::Signal,
                               base::Unretained(&event)));
  task_runner_->PostTask(FROM_HERE, std::move(task));
  event.Wait();
}

template <typename MethodType, typename... Args>
void TPMThreadUtilityImpl::SendRequestAndWait(const MethodType& method,
                                              Args&&... args) {
  base::WaitableEvent event(base::WaitableEvent::ResetPolicy::MANUAL,
                            base::WaitableEvent::InitialState::NOT_SIGNALED);
  base::OnceClosure task =
      base::BindOnce(method, base::Unretained(inner_tpm_.get()),
                     std::forward<Args>(args)...)
          .Then(base::BindOnce(&base::WaitableEvent::Signal,
                               base::Unretained(&event)));
  task_runner_->PostTask(FROM_HERE, std::move(task));
  event.Wait();
}

TPMVersion TPMThreadUtilityImpl::GetTPMVersion() {
  // This simple function doesn't need to be called on the TPM thread.
  return inner_tpm_->GetTPMVersion();
}

size_t TPMThreadUtilityImpl::MinRSAKeyBits() {
  // This simple function doesn't need to be called on the TPM thread.
  return inner_tpm_->MinRSAKeyBits();
}

size_t TPMThreadUtilityImpl::MaxRSAKeyBits() {
  // This simple function doesn't need to be called on the TPM thread.
  return inner_tpm_->MaxRSAKeyBits();
}

bool TPMThreadUtilityImpl::Init() {
  bool result;
  SendRequestAndWaitResult(&TPMUtility::Init, &result);
  return result;
}

bool TPMThreadUtilityImpl::IsTPMAvailable() {
  bool result;
  SendRequestAndWaitResult(&TPMUtility::IsTPMAvailable, &result);
  return result;
}

bool TPMThreadUtilityImpl::Authenticate(const brillo::SecureBlob& auth_data,
                                        const std::string& auth_key_blob,
                                        const std::string& encrypted_root_key,
                                        brillo::SecureBlob* root_key) {
  bool result;
  SendRequestAndWaitResult(&TPMUtility::Authenticate, &result, auth_data,
                           auth_key_blob, encrypted_root_key, root_key);
  return result;
}

bool TPMThreadUtilityImpl::ChangeAuthData(
    const brillo::SecureBlob& old_auth_data,
    const brillo::SecureBlob& new_auth_data,
    const std::string& old_auth_key_blob,
    std::string* new_auth_key_blob) {
  bool result;
  SendRequestAndWaitResult(&TPMUtility::ChangeAuthData, &result, old_auth_data,
                           new_auth_data, old_auth_key_blob, new_auth_key_blob);
  return result;
}

bool TPMThreadUtilityImpl::GenerateRandom(int num_bytes,
                                          std::string* random_data) {
  bool result;
  SendRequestAndWaitResult(&TPMUtility::GenerateRandom, &result, num_bytes,
                           random_data);
  return result;
}

bool TPMThreadUtilityImpl::StirRandom(const std::string& entropy_data) {
  bool result;
  SendRequestAndWaitResult(&TPMUtility::StirRandom, &result, entropy_data);
  return result;
}

bool TPMThreadUtilityImpl::GenerateRSAKey(int slot,
                                          int modulus_bits,
                                          const std::string& public_exponent,
                                          const brillo::SecureBlob& auth_data,
                                          std::string* key_blob,
                                          int* key_handle) {
  bool result;
  SendRequestAndWaitResult(&TPMUtility::GenerateRSAKey, &result, slot,
                           modulus_bits, public_exponent, auth_data, key_blob,
                           key_handle);
  return result;
}

bool TPMThreadUtilityImpl::GetRSAPublicKey(int key_handle,
                                           std::string* public_exponent,
                                           std::string* modulus) {
  bool result;
  SendRequestAndWaitResult(&TPMUtility::GetRSAPublicKey, &result, key_handle,
                           public_exponent, modulus);
  return result;
}

bool TPMThreadUtilityImpl::IsECCurveSupported(int curve_nid) {
  bool result;
  SendRequestAndWaitResult(&TPMUtility::IsECCurveSupported, &result, curve_nid);
  return result;
}

bool TPMThreadUtilityImpl::GenerateECCKey(int slot,
                                          int nid,
                                          const brillo::SecureBlob& auth_data,
                                          std::string* key_blob,
                                          int* key_handle) {
  bool result;
  SendRequestAndWaitResult(&TPMUtility::GenerateECCKey, &result, slot, nid,
                           auth_data, key_blob, key_handle);
  return result;
}

bool TPMThreadUtilityImpl::GetECCPublicKey(int key_handle,
                                           std::string* public_point) {
  bool result;
  SendRequestAndWaitResult(&TPMUtility::GetECCPublicKey, &result, key_handle,
                           public_point);
  return result;
}

bool TPMThreadUtilityImpl::WrapRSAKey(int slot,
                                      const std::string& public_exponent,
                                      const std::string& modulus,
                                      const std::string& prime_factor,
                                      const brillo::SecureBlob& auth_data,
                                      std::string* key_blob,
                                      int* key_handle) {
  bool result;
  SendRequestAndWaitResult(&TPMUtility::WrapRSAKey, &result, slot,
                           public_exponent, modulus, prime_factor, auth_data,
                           key_blob, key_handle);
  return result;
}

bool TPMThreadUtilityImpl::WrapECCKey(int slot,
                                      int curve_nid,
                                      const std::string& public_point_x,
                                      const std::string& public_point_y,
                                      const std::string& private_value,
                                      const brillo::SecureBlob& auth_data,
                                      std::string* key_blob,
                                      int* key_handle) {
  bool result;
  SendRequestAndWaitResult(&TPMUtility::WrapECCKey, &result, slot, curve_nid,
                           public_point_x, public_point_y, private_value,
                           auth_data, key_blob, key_handle);
  return result;
}

bool TPMThreadUtilityImpl::LoadKey(int slot,
                                   const std::string& key_blob,
                                   const brillo::SecureBlob& auth_data,
                                   int* key_handle) {
  bool result;
  SendRequestAndWaitResult(&TPMUtility::LoadKey, &result, slot, key_blob,
                           auth_data, key_handle);
  return result;
}

bool TPMThreadUtilityImpl::LoadKeyWithParent(
    int slot,
    const std::string& key_blob,
    const brillo::SecureBlob& auth_data,
    int parent_key_handle,
    int* key_handle) {
  bool result;
  SendRequestAndWaitResult(&TPMUtility::LoadKeyWithParent, &result, slot,
                           key_blob, auth_data, parent_key_handle, key_handle);
  return result;
}

void TPMThreadUtilityImpl::UnloadKeysForSlot(int slot) {
  SendRequestAndWait(&TPMUtility::UnloadKeysForSlot, slot);
}

bool TPMThreadUtilityImpl::Bind(int key_handle,
                                const std::string& input,
                                std::string* output) {
  bool result;
  SendRequestAndWaitResult(&TPMUtility::Bind, &result, key_handle, input,
                           output);
  return result;
}

bool TPMThreadUtilityImpl::Unbind(int key_handle,
                                  const std::string& input,
                                  std::string* output) {
  bool result;
  SendRequestAndWaitResult(&TPMUtility::Unbind, &result, key_handle, input,
                           output);
  return result;
}

bool TPMThreadUtilityImpl::Sign(int key_handle,
                                CK_MECHANISM_TYPE signing_mechanism,
                                const std::string& mechanism_parameter,
                                const std::string& input,
                                std::string* signature) {
  bool result;
  SendRequestAndWaitResult(&TPMUtility::Sign, &result, key_handle,
                           signing_mechanism, mechanism_parameter, input,
                           signature);
  return result;
}

bool TPMThreadUtilityImpl::IsSRKReady() {
  bool result;
  SendRequestAndWaitResult(&TPMUtility::IsSRKReady, &result);
  return result;
}

bool TPMThreadUtilityImpl::SealData(const std::string& unsealed_data,
                                    const brillo::SecureBlob& auth_value,
                                    std::string* key_blob,
                                    std::string* encrypted_data) {
  bool result;
  SendRequestAndWaitResult(&TPMUtility::SealData, &result, unsealed_data,
                           auth_value, key_blob, encrypted_data);
  return result;
}

bool TPMThreadUtilityImpl::UnsealData(const std::string& key_blob,
                                      const std::string& encrypted_data,
                                      const brillo::SecureBlob& auth_value,
                                      brillo::SecureBlob* unsealed_data) {
  bool result;
  SendRequestAndWaitResult(&TPMUtility::UnsealData, &result, key_blob,
                           encrypted_data, auth_value, unsealed_data);
  return result;
}

void TPMThreadUtilityImpl::GenerateRandomAsync(
    int num_bytes, GenerateRandomCallback callback) {
  CHECK(base::SequencedTaskRunnerHandle::IsSet())
      << "Caller doesn't have task runner.";

  auto random_data = std::make_unique<std::string>();

  base::OnceCallback<bool(void)> task = base::BindOnce(
      &TPMUtility::GenerateRandom, base::Unretained(inner_tpm_.get()),
      num_bytes, random_data.get());

  base::OnceCallback<void(bool)> run_callback = base::BindOnce(
      [](GenerateRandomCallback callback,
         std::unique_ptr<std::string> random_data,
         bool result) { std::move(callback).Run(result, *random_data); },
      std::move(callback), std::move(random_data));

  task_runner_->PostTaskAndReplyWithResult(FROM_HERE, std::move(task),
                                           std::move(run_callback));
}

void TPMThreadUtilityImpl::UnloadKeysForSlotAsync(
    int slot, UnloadKeysForSlotCallback callback) {
  CHECK(base::SequencedTaskRunnerHandle::IsSet())
      << "Caller doesn't have task runner.";

  base::OnceClosure task =
      base::BindOnce(&TPMUtility::UnloadKeysForSlot,
                     base::Unretained(inner_tpm_.get()), slot)
          .Then(base::BindPostTask(base::SequencedTaskRunnerHandle::Get(),
                                   std::move(callback)));

  task_runner_->PostTask(FROM_HERE, std::move(task));
}

void TPMThreadUtilityImpl::SealDataAsync(const std::string& unsealed_data,
                                         const brillo::SecureBlob& auth_value,
                                         SealDataCallback callback) {
  CHECK(base::SequencedTaskRunnerHandle::IsSet())
      << "Caller doesn't have task runner.";

  auto key_blob = std::make_unique<std::string>();
  auto encrypted_data = std::make_unique<std::string>();

  base::OnceCallback<bool(void)> task = base::BindOnce(
      &TPMUtility::SealData, base::Unretained(inner_tpm_.get()), unsealed_data,
      auth_value, key_blob.get(), encrypted_data.get());

  base::OnceCallback<void(bool)> run_callback = base::BindOnce(
      [](SealDataCallback callback, std::unique_ptr<std::string> key_blob,
         std::unique_ptr<std::string> encrypted_data, bool result) {
        std::move(callback).Run(result, *key_blob, *encrypted_data);
      },
      std::move(callback), std::move(key_blob), std::move(encrypted_data));

  task_runner_->PostTaskAndReplyWithResult(FROM_HERE, std::move(task),
                                           std::move(run_callback));
}

void TPMThreadUtilityImpl::UnsealDataAsync(const std::string& key_blob,
                                           const std::string& encrypted_data,
                                           const brillo::SecureBlob& auth_value,
                                           UnsealDataCallback callback) {
  CHECK(base::SequencedTaskRunnerHandle::IsSet())
      << "Caller doesn't have task runner.";

  auto unsealed_data = std::make_unique<brillo::SecureBlob>();

  base::OnceCallback<bool(void)> task = base::BindOnce(
      &TPMUtility::UnsealData, base::Unretained(inner_tpm_.get()), key_blob,
      encrypted_data, auth_value, unsealed_data.get());

  base::OnceCallback<void(bool)> run_callback = base::BindOnce(
      [](UnsealDataCallback callback,
         std::unique_ptr<brillo::SecureBlob> unsealed_data,
         bool result) { std::move(callback).Run(result, *unsealed_data); },
      std::move(callback), std::move(unsealed_data));

  task_runner_->PostTaskAndReplyWithResult(FROM_HERE, std::move(task),
                                           std::move(run_callback));
}

}  // namespace chaps
