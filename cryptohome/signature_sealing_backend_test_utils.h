// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_SIGNATURE_SEALING_BACKEND_TEST_UTILS_H_
#define CRYPTOHOME_SIGNATURE_SEALING_BACKEND_TEST_UTILS_H_

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include <brillo/secure_blob.h>
#include <cryptohome/proto_bindings/key.pb.h>

#include "cryptohome/signature_sealing_backend.h"

namespace cryptohome {

class MockSignatureSealingBackend;
class MockUnsealingSession;

// Creates the SignatureSealedData protobuf message filled with some fake
// values.
hwsec::SignatureSealedData MakeFakeSignatureSealedData(
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
  SignatureSealedCreationMocker(const SignatureSealedCreationMocker&) = delete;
  SignatureSealedCreationMocker& operator=(
      const SignatureSealedCreationMocker&) = delete;

  ~SignatureSealedCreationMocker();

  void set_public_key_spki_der(const brillo::Blob& public_key_spki_der) {
    public_key_spki_der_ = public_key_spki_der;
  }
  void set_key_algorithms(
      const std::vector<structure::ChallengeSignatureAlgorithm>&
          key_algorithms) {
    key_algorithms_ = key_algorithms;
  }
  void set_obfuscated_username(const std::string& obfuscated_username) {
    obfuscated_username_ = obfuscated_username;
  }
  void set_secret_value(const brillo::Blob& secret_value) {
    secret_value_ = secret_value;
  }

  // Sets up the CreateSealedSecret() mock that will report success and return a
  // fake result (see MakeFakeSignatureSealedData()).
  void SetUpSuccessfulMock();
  // Sets up the CreateSealedSecret() mock that will report failure.
  void SetUpFailingMock();

 private:
  MockSignatureSealingBackend* const mock_backend_;
  brillo::Blob public_key_spki_der_;
  std::vector<structure::ChallengeSignatureAlgorithm> key_algorithms_;
  std::string obfuscated_username_;
  brillo::Blob secret_value_;
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
  SignatureSealedUnsealingMocker(const SignatureSealedUnsealingMocker&) =
      delete;
  SignatureSealedUnsealingMocker& operator=(
      const SignatureSealedUnsealingMocker&) = delete;

  ~SignatureSealedUnsealingMocker();

  void set_public_key_spki_der(const brillo::Blob& public_key_spki_der) {
    public_key_spki_der_ = public_key_spki_der;
  }
  void set_key_algorithms(
      const std::vector<structure::ChallengeSignatureAlgorithm>&
          key_algorithms) {
    key_algorithms_ = key_algorithms;
  }
  void set_chosen_algorithm(
      structure::ChallengeSignatureAlgorithm chosen_algorithm) {
    chosen_algorithm_ = chosen_algorithm;
  }
  void set_challenge_value(const brillo::Blob& challenge_value) {
    challenge_value_ = challenge_value;
  }
  void set_challenge_signature(const brillo::Blob& challenge_signature) {
    challenge_signature_ = challenge_signature;
  }
  void set_secret_value(const brillo::Blob& secret_value) {
    secret_value_ = secret_value;
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
  std::vector<structure::ChallengeSignatureAlgorithm> key_algorithms_;
  structure::ChallengeSignatureAlgorithm chosen_algorithm_ =
      structure::ChallengeSignatureAlgorithm::kRsassaPkcs1V15Sha1;
  brillo::Blob challenge_value_;
  brillo::Blob challenge_signature_;
  brillo::Blob secret_value_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_SIGNATURE_SEALING_BACKEND_TEST_UTILS_H_
