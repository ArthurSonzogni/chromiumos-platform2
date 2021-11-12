// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/signature_sealing_backend_test_utils.h"

#include <memory>
#include <string>

#include <gmock/gmock.h>
#include <libhwsec-foundation/error/testing_helper.h>

#include "cryptohome/mock_signature_sealing_backend.h"
#include "cryptohome/protobuf_test_utils.h"
#include "cryptohome/signature_sealing/structures.h"

using brillo::Blob;
using brillo::BlobFromString;
using brillo::BlobToString;
using brillo::SecureBlob;
using ::hwsec::error::TPMError;
using ::hwsec::error::TPMErrorBase;
using ::hwsec::error::TPMRetryAction;
using ::hwsec_foundation::error::testing::ReturnError;
using testing::_;
using testing::AtLeast;
using testing::ByMove;
using testing::DoAll;
using testing::Invoke;
using testing::Return;
using testing::SetArgPointee;
using testing::StrictMock;

namespace cryptohome {

structure::SignatureSealedData MakeFakeSignatureSealedData(
    const Blob& public_key_spki_der) {
  constexpr char kFakeTpm2SrkWrappedSecret[] = "ab";
  structure::SignatureSealedData sealed_data;
  // Fill some fields of the protobuf message just to make test/mock assertions
  // more meaningful. Note that it's unimportant that we use TPM2-specific
  // fields here.
  structure::Tpm2PolicySignedData sealed_data_contents;
  sealed_data_contents.public_key_spki_der = public_key_spki_der;
  sealed_data_contents.srk_wrapped_secret =
      BlobFromString(kFakeTpm2SrkWrappedSecret);
  sealed_data = sealed_data_contents;
  return sealed_data;
}

SignatureSealedCreationMocker::SignatureSealedCreationMocker(
    MockSignatureSealingBackend* mock_backend)
    : mock_backend_(mock_backend) {}

SignatureSealedCreationMocker::~SignatureSealedCreationMocker() = default;

void SignatureSealedCreationMocker::SetUpSuccessfulMock() {
  const structure::SignatureSealedData sealed_data_to_return =
      MakeFakeSignatureSealedData(public_key_spki_der_);
  EXPECT_CALL(*mock_backend_,
              CreateSealedSecret(public_key_spki_der_, key_algorithms_,
                                 default_pcr_map_, extended_pcr_map_,
                                 delegate_blob_, delegate_secret_, _, _))
      .WillOnce(DoAll(SetArgPointee<6>(SecureBlob(secret_value_)),
                      SetArgPointee<7>(sealed_data_to_return),
                      ReturnError<TPMErrorBase>()));
}

void SignatureSealedCreationMocker::SetUpFailingMock() {
  EXPECT_CALL(*mock_backend_,
              CreateSealedSecret(public_key_spki_der_, key_algorithms_,
                                 default_pcr_map_, extended_pcr_map_,
                                 delegate_blob_, delegate_secret_, _, _))
      .WillOnce(ReturnError<TPMError>("fake", TPMRetryAction::kNoRetry));
}

SignatureSealedUnsealingMocker::SignatureSealedUnsealingMocker(
    MockSignatureSealingBackend* mock_backend)
    : mock_backend_(mock_backend) {}

SignatureSealedUnsealingMocker::~SignatureSealedUnsealingMocker() = default;

void SignatureSealedUnsealingMocker::SetUpSuccessfulMock() {
  MockUnsealingSession* mock_unsealing_session = AddSessionCreationMock();
  EXPECT_CALL(*mock_unsealing_session, Unseal(challenge_signature_, _))
      .WillOnce(DoAll(SetArgPointee<1>(SecureBlob(secret_value_)),
                      ReturnError<TPMErrorBase>()));
}

void SignatureSealedUnsealingMocker::SetUpCreationFailingMock(
    bool mock_repeatedly) {
  const structure::SignatureSealedData expected_sealed_data =
      MakeFakeSignatureSealedData(public_key_spki_der_);
  auto& expected_call = EXPECT_CALL(
      *mock_backend_,
      CreateUnsealingSession(StructureEquals(expected_sealed_data),
                             public_key_spki_der_, key_algorithms_, _,
                             delegate_blob_, delegate_secret_, false, _));
  if (mock_repeatedly)
    expected_call.WillRepeatedly(
        ReturnError<TPMError>("fake", TPMRetryAction::kLater));
  else
    expected_call.WillOnce(
        ReturnError<TPMError>("fake", TPMRetryAction::kLater));
}

void SignatureSealedUnsealingMocker::SetUpUsealingFailingMock() {
  MockUnsealingSession* mock_unsealing_session = AddSessionCreationMock();
  EXPECT_CALL(*mock_unsealing_session, Unseal(challenge_signature_, _))
      .WillOnce(ReturnError<TPMError>("fake", TPMRetryAction::kLater));
}

void SignatureSealedUnsealingMocker::SetUpUnsealingNotCalledMock() {
  AddSessionCreationMock();
}

MockUnsealingSession* SignatureSealedUnsealingMocker::AddSessionCreationMock() {
  // The created instance will initially be owned by the
  // CreateUnsealingSession() method mock, which will then transfer the
  // ownership to its caller.
  StrictMock<MockUnsealingSession>* mock_unsealing_session =
      new StrictMock<MockUnsealingSession>;
  const structure::SignatureSealedData expected_sealed_data =
      MakeFakeSignatureSealedData(public_key_spki_der_);
  EXPECT_CALL(*mock_backend_,
              CreateUnsealingSession(StructureEquals(expected_sealed_data),
                                     public_key_spki_der_, key_algorithms_, _,
                                     delegate_blob_, delegate_secret_,
                                     /*locked_to_single_user=*/false, _))
      .WillOnce(Invoke([mock_unsealing_session](auto&&, auto&&, auto&&, auto&&,
                                                auto&&, auto&&, auto&&,
                                                auto* unsealing_session) {
        *unsealing_session = std::unique_ptr<StrictMock<MockUnsealingSession>>(
            mock_unsealing_session);
        return nullptr;
      }))
      .RetiresOnSaturation();
  EXPECT_CALL(*mock_unsealing_session, GetChallengeAlgorithm())
      .WillRepeatedly(Return(chosen_algorithm_));
  EXPECT_CALL(*mock_unsealing_session, GetChallengeValue())
      .WillRepeatedly(Return(challenge_value_));
  return mock_unsealing_session;
}

}  // namespace cryptohome
