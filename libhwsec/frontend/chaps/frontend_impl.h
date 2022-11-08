// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_FRONTEND_CHAPS_FRONTEND_IMPL_H_
#define LIBHWSEC_FRONTEND_CHAPS_FRONTEND_IMPL_H_

#include <string>
#include <vector>

#include <brillo/secure_blob.h>

#include "libhwsec/frontend/chaps/frontend.h"
#include "libhwsec/frontend/frontend_impl.h"
#include "libhwsec/status.h"
#include "libhwsec/structures/key.h"

namespace hwsec {

class ChapsFrontendImpl : public ChapsFrontend, public FrontendImpl {
 public:
  using FrontendImpl::FrontendImpl;
  ~ChapsFrontendImpl() override = default;

  StatusOr<uint32_t> GetFamily() override;
  StatusOr<bool> IsEnabled() override;
  StatusOr<bool> IsReady() override;
  StatusOr<brillo::Blob> GetRandomBlob(size_t size) override;
  StatusOr<brillo::SecureBlob> GetRandomSecureBlob(size_t size) override;
  Status IsRSAModulusSupported(uint32_t modulus_bits) override;
  Status IsECCurveSupported(int nid) override;
  StatusOr<CreateKeyResult> GenerateRSAKey(
      int modulus_bits,
      const brillo::Blob& public_exponent,
      const brillo::SecureBlob& auth_value) override;
  StatusOr<RSAPublicInfo> GetRSAPublicKey(Key key) override;
  StatusOr<CreateKeyResult> GenerateECCKey(
      int nid, const brillo::SecureBlob& auth_value) override;
  StatusOr<ECCPublicInfo> GetECCPublicKey(Key key) override;
  StatusOr<CreateKeyResult> WrapRSAKey(
      const brillo::Blob& exponent,
      const brillo::Blob& modulus,
      const brillo::SecureBlob& prime_factor,
      const brillo::SecureBlob& auth_value) override;
  StatusOr<CreateKeyResult> WrapECCKey(
      int curve_nid,
      const brillo::Blob& public_point_x,
      const brillo::Blob& public_point_y,
      const brillo::SecureBlob& private_value,
      const brillo::SecureBlob& auth_value) override;
  StatusOr<ScopedKey> LoadKey(const brillo::Blob& key_blob,
                              const brillo::SecureBlob& auth_value) override;
  StatusOr<brillo::SecureBlob> Unbind(Key key,
                                      const brillo::Blob& ciphertext) override;
  StatusOr<brillo::Blob> Sign(Key key,
                              const brillo::Blob& data,
                              const SigningOptions& options) override;
  StatusOr<ChapsSealedData> SealData(
      const brillo::SecureBlob& unsealed_data,
      const brillo::SecureBlob& auth_value) override;
  StatusOr<brillo::SecureBlob> UnsealData(
      const ChapsSealedData& sealed_data,
      const brillo::SecureBlob& auth_value) override;
  void GetRandomSecureBlobAsync(size_t size,
                                GetRandomSecureBlobCallback callback) override;
  void SealDataAsync(const brillo::SecureBlob& unsealed_data,
                     const brillo::SecureBlob& auth_value,
                     SealDataCallback callback) override;
  void UnsealDataAsync(const ChapsSealedData& sealed_data,
                       const brillo::SecureBlob& auth_value,
                       UnsealDataCallback callback) override;
};

}  // namespace hwsec

#endif  // LIBHWSEC_FRONTEND_CHAPS_FRONTEND_IMPL_H_
