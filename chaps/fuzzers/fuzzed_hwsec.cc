// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chaps/fuzzers/fuzzed_hwsec.h"

#include <string>
#include <utility>
#include <vector>

#include <base/callback.h>
#include <base/task/task_runner.h>
#include <base/threading/thread_task_runner_handle.h>
#include <base/threading/thread.h>
#include <brillo/secure_blob.h>
#include <libhwsec/frontend/chaps/frontend.h>
#include <libhwsec/status.h>
#include <libhwsec-foundation/status/status_chain_macros.h>

using hwsec_foundation::status::MakeStatus;

namespace hwsec {

namespace {
[[clang::return_typestate(unconsumed)]] Status SimpleError() {
  return MakeStatus<TPMError>("Simple", TPMRetryAction::kNoRetry);
}

MiddlewareDerivative GetMiddlewareDerivative() {
  return MiddlewareDerivative{
      .task_runner = base::SequencedTaskRunnerHandle::IsSet()
                         ? base::SequencedTaskRunnerHandle::Get()
                         : nullptr,
      .thread_id = base::PlatformThread::CurrentId(),
      .middleware = nullptr,
  };
}

ScopedKey GetTestScopedKey() {
  return ScopedKey(Key{.token = 42}, GetMiddlewareDerivative());
}
}  // namespace

StatusOr<uint32_t> FuzzedChapsFrontend::GetFamily() {
  if (data_provider_->ConsumeBool()) {
    return SimpleError();
  }
  return data_provider_->ConsumeIntegral<uint32_t>();
}

StatusOr<bool> FuzzedChapsFrontend::IsEnabled() {
  if (data_provider_->ConsumeBool()) {
    return SimpleError();
  }
  return data_provider_->ConsumeBool();
}

StatusOr<bool> FuzzedChapsFrontend::IsReady() {
  if (data_provider_->ConsumeBool()) {
    return SimpleError();
  }
  return data_provider_->ConsumeBool();
}

StatusOr<brillo::Blob> FuzzedChapsFrontend::GetRandomBlob(size_t size) {
  if (data_provider_->ConsumeBool()) {
    return SimpleError();
  }
  return brillo::BlobFromString(data_provider_->ConsumeRandomLengthString());
}

StatusOr<brillo::SecureBlob> FuzzedChapsFrontend::GetRandomSecureBlob(
    size_t size) {
  if (data_provider_->ConsumeBool()) {
    return SimpleError();
  }
  return brillo::SecureBlob(data_provider_->ConsumeRandomLengthString());
}

Status FuzzedChapsFrontend::IsRSAModulusSupported(uint32_t modulus_bits) {
  if (data_provider_->ConsumeBool()) {
    return SimpleError();
  }
  return OkStatus();
}

Status FuzzedChapsFrontend::IsECCurveSupported(int nid) {
  if (data_provider_->ConsumeBool()) {
    return SimpleError();
  }
  return OkStatus();
}

StatusOr<ChapsFrontend::CreateKeyResult> FuzzedChapsFrontend::GenerateRSAKey(
    int modulus_bits,
    const brillo::Blob& public_exponent,
    const brillo::SecureBlob& auth_value) {
  if (data_provider_->ConsumeBool()) {
    return SimpleError();
  }
  return ChapsFrontend::CreateKeyResult{
      .key = GetTestScopedKey(),
      .key_blob =
          brillo::BlobFromString(data_provider_->ConsumeRandomLengthString()),
  };
}

StatusOr<RSAPublicInfo> FuzzedChapsFrontend::GetRSAPublicKey(Key key) {
  if (data_provider_->ConsumeBool()) {
    return SimpleError();
  }
  return RSAPublicInfo{
      .exponent =
          brillo::BlobFromString(data_provider_->ConsumeRandomLengthString()),
      .modulus =
          brillo::BlobFromString(data_provider_->ConsumeRandomLengthString()),
  };
}

StatusOr<ChapsFrontend::CreateKeyResult> FuzzedChapsFrontend::GenerateECCKey(
    int nid, const brillo::SecureBlob& auth_value) {
  if (data_provider_->ConsumeBool()) {
    return SimpleError();
  }
  return ChapsFrontend::CreateKeyResult{
      .key = GetTestScopedKey(),
      .key_blob =
          brillo::BlobFromString(data_provider_->ConsumeRandomLengthString()),
  };
}

StatusOr<ECCPublicInfo> FuzzedChapsFrontend::GetECCPublicKey(Key key) {
  if (data_provider_->ConsumeBool()) {
    return SimpleError();
  }
  return ECCPublicInfo{
      .nid = data_provider_->ConsumeIntegral<int>(),
      .x_point =
          brillo::BlobFromString(data_provider_->ConsumeRandomLengthString()),
      .y_point =
          brillo::BlobFromString(data_provider_->ConsumeRandomLengthString()),
  };
}

StatusOr<hwsec::ChapsFrontend::CreateKeyResult> FuzzedChapsFrontend::WrapRSAKey(
    const brillo::Blob& exponent,
    const brillo::Blob& modulus,
    const brillo::SecureBlob& prime_factor,
    const brillo::SecureBlob& auth_value) {
  if (data_provider_->ConsumeBool()) {
    return SimpleError();
  }
  return ChapsFrontend::CreateKeyResult{
      .key = GetTestScopedKey(),
      .key_blob =
          brillo::BlobFromString(data_provider_->ConsumeRandomLengthString()),
  };
}

StatusOr<hwsec::ChapsFrontend::CreateKeyResult> FuzzedChapsFrontend::WrapECCKey(
    int curve_nid,
    const brillo::Blob& public_point_x,
    const brillo::Blob& public_point_y,
    const brillo::SecureBlob& private_value,
    const brillo::SecureBlob& auth_value) {
  if (data_provider_->ConsumeBool()) {
    return SimpleError();
  }
  return ChapsFrontend::CreateKeyResult{
      .key = GetTestScopedKey(),
      .key_blob =
          brillo::BlobFromString(data_provider_->ConsumeRandomLengthString()),
  };
}

StatusOr<hwsec::ScopedKey> FuzzedChapsFrontend::LoadKey(
    const brillo::Blob& key_blob, const brillo::SecureBlob& auth_value) {
  if (data_provider_->ConsumeBool()) {
    return SimpleError();
  }
  return GetTestScopedKey();
}

StatusOr<brillo::SecureBlob> FuzzedChapsFrontend::Unbind(
    Key key, const brillo::Blob& ciphertext) {
  if (data_provider_->ConsumeBool()) {
    return SimpleError();
  }
  return brillo::SecureBlob(data_provider_->ConsumeRandomLengthString());
}

StatusOr<brillo::Blob> FuzzedChapsFrontend::Sign(
    Key key, const brillo::Blob& data, const SigningOptions& options) {
  if (data_provider_->ConsumeBool()) {
    return SimpleError();
  }
  return brillo::BlobFromString(data_provider_->ConsumeRandomLengthString());
}

StatusOr<hwsec::ChapsSealedData> FuzzedChapsFrontend::SealData(
    const brillo::SecureBlob& unsealed_data,
    const brillo::SecureBlob& auth_value) {
  if (data_provider_->ConsumeBool()) {
    return SimpleError();
  }
  return ChapsSealedData{
      .key_blob =
          brillo::BlobFromString(data_provider_->ConsumeRandomLengthString()),
      .encrypted_data =
          brillo::BlobFromString(data_provider_->ConsumeRandomLengthString()),
  };
}

StatusOr<brillo::SecureBlob> FuzzedChapsFrontend::UnsealData(
    const ChapsSealedData& sealed_data, const brillo::SecureBlob& auth_value) {
  if (data_provider_->ConsumeBool()) {
    return SimpleError();
  }
  return brillo::SecureBlob(data_provider_->ConsumeRandomLengthString());
}

void FuzzedChapsFrontend::GetRandomSecureBlobAsync(
    size_t size, GetRandomSecureBlobCallback callback) {
  base::SequencedTaskRunnerHandle::Get()->PostTask(
      FROM_HERE, base::BindOnce(&FuzzedChapsFrontend::GetRandomSecureBlob,
                                base::Unretained(this), size)
                     .Then(std::move(callback)));
}

void FuzzedChapsFrontend::SealDataAsync(const brillo::SecureBlob& unsealed_data,
                                        const brillo::SecureBlob& auth_value,
                                        SealDataCallback callback) {
  base::SequencedTaskRunnerHandle::Get()->PostTask(
      FROM_HERE,
      base::BindOnce(&FuzzedChapsFrontend::SealData, base::Unretained(this),
                     unsealed_data, auth_value)
          .Then(std::move(callback)));
}

void FuzzedChapsFrontend::UnsealDataAsync(const ChapsSealedData& sealed_data,
                                          const brillo::SecureBlob& auth_value,
                                          UnsealDataCallback callback) {
  base::SequencedTaskRunnerHandle::Get()->PostTask(
      FROM_HERE, base::BindOnce(&FuzzedChapsFrontend::UnsealData,
                                base::Unretained(this), sealed_data, auth_value)
                     .Then(std::move(callback)));
}

}  // namespace hwsec
