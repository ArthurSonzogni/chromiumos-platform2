// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_FRONTEND_CHAPS_MOCK_FRONTEND_H_
#define LIBHWSEC_FRONTEND_CHAPS_MOCK_FRONTEND_H_

#include <string>
#include <vector>

#include <absl/container/flat_hash_set.h>
#include <brillo/secure_blob.h>
#include <gmock/gmock.h>

#include "libhwsec/frontend/chaps/frontend.h"
#include "libhwsec/frontend/mock_frontend.h"

namespace hwsec {

class MockChapsFrontend : public MockFrontend, public ChapsFrontend {
 public:
  MockChapsFrontend() = default;
  ~MockChapsFrontend() override = default;

  MOCK_METHOD(StatusOr<uint32_t>, GetFamily, (), (override));
  MOCK_METHOD(StatusOr<bool>, IsEnabled, (), (override));
  MOCK_METHOD(StatusOr<bool>, IsReady, (), (override));
  MOCK_METHOD(StatusOr<brillo::Blob>, GetRandomBlob, (size_t size), (override));
  MOCK_METHOD(StatusOr<brillo::SecureBlob>,
              GetRandomSecureBlob,
              (size_t),
              (override));
  MOCK_METHOD(Status,
              IsRSAModulusSupported,
              (uint32_t modulus_bits),
              (override));
  MOCK_METHOD(Status, IsECCurveSupported, (int nid), (override));
  MOCK_METHOD(StatusOr<CreateKeyResult>,
              GenerateRSAKey,
              (int modulus_bits,
               const brillo::Blob& public_exponent,
               const brillo::SecureBlob& auth_value),
              (override));
  MOCK_METHOD(StatusOr<RSAPublicInfo>, GetRSAPublicKey, (Key key), (override));
  MOCK_METHOD(StatusOr<CreateKeyResult>,
              GenerateECCKey,
              (int nid, const brillo::SecureBlob& auth_value),
              (override));
  MOCK_METHOD(StatusOr<ECCPublicInfo>, GetECCPublicKey, (Key key), (override));
  MOCK_METHOD(StatusOr<CreateKeyResult>,
              WrapRSAKey,
              (const brillo::Blob& exponent,
               const brillo::Blob& modulus,
               const brillo::SecureBlob& prime_factor,
               const brillo::SecureBlob& auth_value),
              (override));
  MOCK_METHOD(StatusOr<CreateKeyResult>,
              WrapECCKey,
              (int curve_nid,
               const brillo::Blob& public_point_x,
               const brillo::Blob& public_point_y,
               const brillo::SecureBlob& private_value,
               const brillo::SecureBlob& auth_value),
              (override));
  MOCK_METHOD(StatusOr<ScopedKey>,
              LoadKey,
              (const brillo::Blob& key_blob,
               const brillo::SecureBlob& auth_value),
              (override));
  MOCK_METHOD(StatusOr<brillo::SecureBlob>,
              Unbind,
              (Key key, const brillo::Blob& ciphertext),
              (override));
  MOCK_METHOD(StatusOr<brillo::Blob>,
              Sign,
              (Key key,
               const brillo::Blob& data,
               const SigningOptions& options),
              (override));
  MOCK_METHOD(StatusOr<ChapsSealedData>,
              SealData,
              (const brillo::SecureBlob& unsealed_data,
               const brillo::SecureBlob& auth_value),
              (override));
  MOCK_METHOD(StatusOr<brillo::SecureBlob>,
              UnsealData,
              (const ChapsSealedData& sealed_data,
               const brillo::SecureBlob& auth_value),
              (override));
  MOCK_METHOD(void,
              GetRandomSecureBlobAsync,
              (size_t size, GetRandomSecureBlobCallback callback),
              (override));
  MOCK_METHOD(void,
              SealDataAsync,
              (const brillo::SecureBlob& unsealed_data,
               const brillo::SecureBlob& auth_value,
               SealDataCallback callback),
              (override));
  MOCK_METHOD(void,
              UnsealDataAsync,
              (const ChapsSealedData& sealed_data,
               const brillo::SecureBlob& auth_value,
               UnsealDataCallback callback),
              (override));
};

}  // namespace hwsec

#endif  // LIBHWSEC_FRONTEND_CHAPS_MOCK_FRONTEND_H_
