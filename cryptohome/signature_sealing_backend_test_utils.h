// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_SIGNATURE_SEALING_BACKEND_TEST_UTILS_H_
#define CRYPTOHOME_SIGNATURE_SEALING_BACKEND_TEST_UTILS_H_

#include <cstdint>
#include <map>
#include <vector>

#include <base/macros.h>
#include <brillo/secure_blob.h>

#include "cryptohome/signature_sealing_backend.h"

namespace cryptohome {

class MockSignatureSealingBackend;
class MockUnsealingSession;

// Creates the SignatureSealedData protobuf message filled with some fake
// values.
SignatureSealedData MakeFakeSignatureSealedData(
    const brillo::Blob& public_key_spki_der);

// Helper for setting up mock expectation and mock response for the
// signature-sealed secret creation functionality (the
// MockSignatureSealingBackend::CreateSealedSecret() method).
//
// This class follows the "builder" pattern - i.e., first use the set_*()
// methods to set up expected parameters, and then call one of the SetUp*Mock()
// methods to actually set up the mock expectation with the desired behavior.
class SignatureSealedCreationMocker final {
 public:
  explicit SignatureSealedCreationMocker(
      MockSignatureSealingBackend* mock_backend);
  ~SignatureSealedCreationMocker();

  void set_public_key_spki_der(const brillo::Blob& public_key_spki_der) {
    public_key_spki_der_ = public_key_spki_der;
  }
  void set_key_algorithms(
      const std::vector<SignatureSealingBackend::Algorithm>& key_algorithms) {
    key_algorithms_ = key_algorithms;
  }
  void set_pcr_values(const std::map<uint32_t, brillo::Blob>& pcr_values) {
    pcr_values_ = pcr_values;
  }
  void set_delegate_blob(const brillo::Blob& delegate_blob) {
    delegate_blob_ = delegate_blob;
  }
  void set_delegate_secret(const brillo::Blob& delegate_secret) {
    delegate_secret_ = delegate_secret;
  }

  // Sets up the CreateSealedSecret() mock that will report success and return a
  // fake result (see MakeFakeSignatureSealedData()).
  void SetUpSuccessfulMock();
  // Sets up the CreateSealedSecret() mock that will report failure.
  void SetUpFailingMock();

 private:
  MockSignatureSealingBackend* const mock_backend_;
  brillo::Blob public_key_spki_der_;
  std::vector<SignatureSealingBackend::Algorithm> key_algorithms_;
  std::map<uint32_t, brillo::Blob> pcr_values_;
  brillo::Blob delegate_blob_;
  brillo::Blob delegate_secret_;

  DISALLOW_COPY_AND_ASSIGN(SignatureSealedCreationMocker);
};

// Helper for setting up mock expectation and mock response for the
// unsealing functionality of signature-sealed secret (see
// MockSignatureSealingBackend::CreateUnsealingSession() and
// MockUnsealingSession).
//
// This class follows the "builder" pattern - i.e., first use the set_*()
// methods to set up expected parameters and values to be returned, and then
// call one of the SetUp*Mock() methods to actually set up the mock expectation
// with the desired behavior.
class SignatureSealedUnsealingMocker final {
 public:
  explicit SignatureSealedUnsealingMocker(
      MockSignatureSealingBackend* mock_backend);
  ~SignatureSealedUnsealingMocker();

  void set_public_key_spki_der(const brillo::Blob& public_key_spki_der) {
    public_key_spki_der_ = public_key_spki_der;
  }
  void set_key_algorithms(
      const std::vector<SignatureSealingBackend::Algorithm>& key_algorithms) {
    key_algorithms_ = key_algorithms;
  }
  void set_delegate_blob(const brillo::Blob& delegate_blob) {
    delegate_blob_ = delegate_blob;
  }
  void set_delegate_secret(const brillo::Blob& delegate_secret) {
    delegate_secret_ = delegate_secret;
  }
  void set_chosen_algorithm(
      SignatureSealingBackend::Algorithm chosen_algorithm) {
    chosen_algorithm_ = chosen_algorithm;
  }
  void set_challenge_value(const brillo::Blob& challenge_value) {
    challenge_value_ = challenge_value;
  }
  void set_challenge_signature(const brillo::Blob& challenge_signature) {
    challenge_signature_ = challenge_signature;
  }
  void set_unsealed_secret(const brillo::Blob& unsealed_secret) {
    unsealed_secret_ = unsealed_secret;
  }

  // Sets up mocks that will simulate the successful unsealing.
  void SetUpSuccessfulMock();
  // Sets up mocks that will report failure from
  // MockSignatureSealingBackend::CreateUnsealingSession().
  void SetUpCreationFailingMock(bool mock_repeatedly);
  // Sets up mocks that will report failure from
  // MockUnsealingSession::Unseal().
  void SetUpUsealingFailingMock();
  // Sets up mocks that report success from
  // MockSignatureSealingBackend::CreateUnsealingSession(), but with the
  // expectation that MockUnsealingSession::Unseal() is not called.
  void SetUpUnsealingNotCalledMock();

 private:
  MockUnsealingSession* AddSessionCreationMock();

  MockSignatureSealingBackend* const mock_backend_;
  brillo::Blob public_key_spki_der_;
  std::vector<SignatureSealingBackend::Algorithm> key_algorithms_;
  brillo::Blob delegate_blob_;
  brillo::Blob delegate_secret_;
  SignatureSealingBackend::Algorithm chosen_algorithm_ =
      SignatureSealingBackend::Algorithm::kRsassaPkcs1V15Sha1;
  brillo::Blob challenge_value_;
  brillo::Blob challenge_signature_;
  brillo::Blob unsealed_secret_;

  DISALLOW_COPY_AND_ASSIGN(SignatureSealedUnsealingMocker);
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_SIGNATURE_SEALING_BACKEND_TEST_UTILS_H_
