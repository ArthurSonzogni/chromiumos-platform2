// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_MOCK_SIGNATURE_SEALING_BACKEND_H_
#define CRYPTOHOME_MOCK_SIGNATURE_SEALING_BACKEND_H_

#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <brillo/secure_blob.h>
#include <gmock/gmock.h>

#include "cryptohome/signature_sealing_backend.h"

namespace cryptohome {

class SignatureSealedData;

class MockSignatureSealingBackend : public SignatureSealingBackend {
 public:
  MockSignatureSealingBackend();
  ~MockSignatureSealingBackend() override;

  MOCK_METHOD(hwsec::StatusChain<hwsec::TPMErrorBase>,
              CreateSealedSecret,
              (const brillo::Blob&,
               const std::vector<structure::ChallengeSignatureAlgorithm>&,
               (const std::map<uint32_t, brillo::Blob>&),
               (const std::map<uint32_t, brillo::Blob>&),
               const brillo::Blob&,
               const brillo::Blob&,
               brillo::SecureBlob*,
               structure::SignatureSealedData*),
              (override));

  MOCK_METHOD(hwsec::StatusChain<hwsec::TPMErrorBase>,
              CreateUnsealingSession,
              (const structure::SignatureSealedData& sealed_secret_data,
               const brillo::Blob& public_key_spki_der,
               const std::vector<structure::ChallengeSignatureAlgorithm>&
                   key_algorithms,
               const std::set<uint32_t>&,
               const brillo::Blob& delegate_blob,
               const brillo::Blob& delegate_secret,
               bool locked_to_single_user,
               std::unique_ptr<UnsealingSession>* unsealing_session),
              (override));
};

class MockUnsealingSession : public SignatureSealingBackend::UnsealingSession {
 public:
  MockUnsealingSession();
  ~MockUnsealingSession() override;

  MOCK_METHOD(structure::ChallengeSignatureAlgorithm,
              GetChallengeAlgorithm,
              (),
              (override));
  MOCK_METHOD(brillo::Blob, GetChallengeValue, (), (override));
  MOCK_METHOD(hwsec::StatusChain<hwsec::TPMErrorBase>,
              Unseal,
              (const brillo::Blob&, brillo::SecureBlob*),
              (override));
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_MOCK_SIGNATURE_SEALING_BACKEND_H_
