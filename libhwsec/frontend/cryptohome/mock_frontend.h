// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_FRONTEND_CRYPTOHOME_MOCK_FRONTEND_H_
#define LIBHWSEC_FRONTEND_CRYPTOHOME_MOCK_FRONTEND_H_

#include <string>
#include <vector>

#include <absl/container/flat_hash_set.h>
#include <brillo/secure_blob.h>
#include <gmock/gmock.h>

#include "libhwsec/frontend/cryptohome/frontend.h"
#include "libhwsec/frontend/mock_frontend.h"

namespace hwsec {

class MockCryptohomeFrontend : public MockFrontend, public CryptohomeFrontend {
 public:
  MockCryptohomeFrontend() = default;
  ~MockCryptohomeFrontend() override = default;

  MOCK_METHOD(StatusOr<bool>, IsEnabled, (), (override));
  MOCK_METHOD(StatusOr<bool>, IsReady, (), (override));
  MOCK_METHOD(StatusOr<bool>, IsDAMitigationReady, (), (override));
  MOCK_METHOD(StatusOr<bool>, IsSrkRocaVulnerable, (), (override));
  MOCK_METHOD(Status, MitigateDACounter, (), (override));
  MOCK_METHOD(StatusOr<brillo::Blob>, GetRsuDeviceId, (), (override));
  MOCK_METHOD(StatusOr<absl::flat_hash_set<KeyAlgoType>>,
              GetSupportedAlgo,
              (),
              (override));
  MOCK_METHOD(StatusOr<CreateKeyResult>,
              CreateCryptohomeKey,
              (KeyAlgoType),
              (override));
  MOCK_METHOD(StatusOr<ScopedKey>, LoadKey, (const brillo::Blob&), (override));
  MOCK_METHOD(StatusOr<brillo::Blob>, GetPubkeyHash, (Key), (override));
  MOCK_METHOD(StatusOr<ScopedKey>, SideLoadKey, (uint32_t), (override));
  MOCK_METHOD(StatusOr<uint32_t>, GetKeyHandle, (Key), (override));
  MOCK_METHOD(Status, SetCurrentUser, (const std::string&), (override));
  MOCK_METHOD(StatusOr<bool>, IsCurrentUserSet, (), (override));
  MOCK_METHOD(StatusOr<brillo::Blob>,
              SealWithCurrentUser,
              (const std::optional<std::string>&,
               const brillo::SecureBlob&,
               const brillo::SecureBlob&),
              (override));
  MOCK_METHOD(StatusOr<std::optional<ScopedKey>>,
              PreloadSealedData,
              (const brillo::Blob&),
              (override));
  MOCK_METHOD(StatusOr<brillo::SecureBlob>,
              UnsealWithCurrentUser,
              (std::optional<Key>,
               const brillo::SecureBlob&,
               const brillo::Blob&),
              (override));
  MOCK_METHOD(StatusOr<brillo::Blob>,
              Encrypt,
              (Key, const brillo::SecureBlob&),
              (override));
  MOCK_METHOD(StatusOr<brillo::SecureBlob>,
              Decrypt,
              (Key, const brillo::Blob&),
              (override));
  MOCK_METHOD(StatusOr<brillo::SecureBlob>,
              GetAuthValue,
              (Key, const brillo::SecureBlob&),
              (override));
  MOCK_METHOD(StatusOr<brillo::Blob>, GetRandomBlob, (size_t), (override));
  MOCK_METHOD(StatusOr<brillo::SecureBlob>,
              GetRandomSecureBlob,
              (size_t),
              (override));
  MOCK_METHOD(StatusOr<uint32_t>, GetManufacturer, (), (override));
  MOCK_METHOD(StatusOr<bool>, IsPinWeaverEnabled, (), (override));
  MOCK_METHOD(StatusOr<StorageState>, GetSpaceState, (Space), (override));
  MOCK_METHOD(Status, PrepareSpace, (Space, uint32_t), (override));
  MOCK_METHOD(StatusOr<brillo::Blob>, LoadSpace, (Space), (override));
  MOCK_METHOD(Status, StoreSpace, (Space, const brillo::Blob&), (override));
  MOCK_METHOD(Status, DestroySpace, (Space), (override));
  MOCK_METHOD(StatusOr<bool>, IsSpaceWriteLocked, (Space), (override));
  MOCK_METHOD(Status, DeclareTpmFirmwareStable, (), (override));
  MOCK_METHOD(StatusOr<SignatureSealedData>,
              SealWithSignatureAndCurrentUser,
              (const std::string& current_user,
               const brillo::SecureBlob&,
               const brillo::Blob&,
               const std::vector<SignatureSealingAlgorithm>&),
              (override));
  MOCK_METHOD(StatusOr<ChallengeResult>,
              ChallengeWithSignatureAndCurrentUser,
              (const SignatureSealedData& sealed_data,
               const brillo::Blob& public_key_spki_der,
               const std::vector<SignatureSealingAlgorithm>& key_algorithms),
              (override));
  MOCK_METHOD(StatusOr<brillo::SecureBlob>,
              UnsealWithChallenge,
              (ChallengeID challenge, const brillo::Blob& challenge_response),
              (override));
};

}  // namespace hwsec

#endif  // LIBHWSEC_FRONTEND_CRYPTOHOME_MOCK_FRONTEND_H_
