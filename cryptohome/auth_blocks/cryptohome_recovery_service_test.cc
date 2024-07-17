// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_blocks/cryptohome_recovery_service.h"

#include <memory>

#include <base/test/task_environment.h>
#include <base/test/test_future.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <libhwsec-foundation/error/testing_helper.h>
#include <libhwsec/factory/tpm2_simulator_factory_for_test.h>
#include <libhwsec/frontend/recovery_crypto/frontend.h>

#include "cryptohome/cryptorecovery/fake_recovery_mediator_crypto.h"
#include "cryptohome/cryptorecovery/recovery_crypto_hsm_cbor_serialization.h"
#include "cryptohome/cryptorecovery/recovery_crypto_impl.h"
#include "cryptohome/error/cryptohome_error.h"
#include "cryptohome/fake_platform.h"
#include "cryptohome/username.h"

namespace cryptohome {
namespace {

using ::hwsec_foundation::error::testing::IsOk;
using ::hwsec_foundation::error::testing::NotOk;
using ::testing::_;
using ::testing::IsEmpty;
using ::testing::IsTrue;
using ::testing::Not;
using ::testing::NotNull;
using ::testing::Optional;

// Base test fixture which sets up the task environment.
class BaseTestFixture : public ::testing::Test {
 protected:
  base::test::SingleThreadTaskEnvironment task_environment_ = {
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  scoped_refptr<base::SequencedTaskRunner> task_runner_ =
      base::SequencedTaskRunner::GetCurrentDefault();
};

// Test fixture for tests with a standard service instance constructed using a
// simulated TPM and fake recovery auth block state.
class CryptohomeRecoveryAuthBlockServiceTest : public BaseTestFixture {
 public:
  CryptohomeRecoveryAuthBlockServiceTest()
      : recovery_hwsec_(hwsec_factory_.GetRecoveryCryptoFrontend()),
        service_(&platform_, recovery_hwsec_.get()) {
    brillo::Blob mediator_pub_key;
    EXPECT_THAT(
        cryptorecovery::FakeRecoveryMediatorCrypto::GetFakeMediatorPublicKey(
            &mediator_pub_key),
        IsTrue());
    cryptorecovery::CryptoRecoveryEpochResponse epoch_response;
    EXPECT_THAT(
        cryptorecovery::FakeRecoveryMediatorCrypto::GetFakeEpochResponse(
            &epoch_response),
        IsTrue());
    epoch_response_blob_ =
        brillo::BlobFromString(epoch_response.SerializeAsString());

    auto recovery = cryptorecovery::RecoveryCryptoImpl::Create(
        recovery_hwsec_.get(), &platform_);
    EXPECT_THAT(recovery, NotNull());

    cryptorecovery::HsmPayload hsm_payload;
    brillo::SecureBlob recovery_key;
    cryptorecovery::GenerateHsmPayloadRequest generate_hsm_payload_request(
        {.mediator_pub_key = mediator_pub_key,
         .onboarding_metadata = cryptorecovery::OnboardingMetadata{},
         .obfuscated_username = kObfuscatedUsername});
    cryptorecovery::GenerateHsmPayloadResponse generate_hsm_payload_response;
    EXPECT_THAT(recovery->GenerateHsmPayload(generate_hsm_payload_request,
                                             &generate_hsm_payload_response),
                IsTrue());
    auth_block_state_.encrypted_rsa_priv_key =
        generate_hsm_payload_response.encrypted_rsa_priv_key;
    auth_block_state_.encrypted_destination_share =
        generate_hsm_payload_response.encrypted_destination_share;
    auth_block_state_.channel_pub_key =
        generate_hsm_payload_response.channel_pub_key;
    auth_block_state_.encrypted_channel_priv_key =
        generate_hsm_payload_response.encrypted_channel_priv_key;
    recovery_key = generate_hsm_payload_response.recovery_key;
    EXPECT_THAT(cryptorecovery::SerializeHsmPayloadToCbor(
                    generate_hsm_payload_response.hsm_payload,
                    &auth_block_state_.hsm_payload),
                IsTrue());
  }

 protected:
  // Alias for a test future that can be used as a prepare token consumer.
  using TokenConsumerFuture = base::test::TestFuture<
      CryptohomeStatusOr<std::unique_ptr<PreparedAuthFactorToken>>>;

  FakePlatform platform_;
  hwsec::Tpm2SimulatorFactoryForTest hwsec_factory_;
  std::unique_ptr<const hwsec::RecoveryCryptoFrontend> recovery_hwsec_;
  CryptohomeRecoveryAuthBlockService service_;

  // Constants for use in testing.
  const ObfuscatedUsername kObfuscatedUsername{"obfuscated_username"};

  // Pre-generated recovery state and blobs for use in testing.
  brillo::Blob epoch_response_blob_;
  CryptohomeRecoveryAuthBlockState auth_block_state_;
};

TEST_F(CryptohomeRecoveryAuthBlockServiceTest, GenerateRecoveryRequestSuccess) {
  TokenConsumerFuture token_future;
  service_.GenerateRecoveryRequest(
      kObfuscatedUsername, cryptorecovery::RequestMetadata{},
      epoch_response_blob_, auth_block_state_, token_future.GetCallback());

  auto token = token_future.Take();
  ASSERT_THAT(token, IsOk());
  ASSERT_THAT(*token, NotNull());
  const auto& prepare_output =
      (*token)->prepare_output().cryptohome_recovery_prepare_output;
  ASSERT_THAT(prepare_output, Optional(_));
  EXPECT_THAT(prepare_output->ephemeral_pub_key, Not(IsEmpty()));
}

TEST_F(CryptohomeRecoveryAuthBlockServiceTest,
       GenerateRecoveryRequestNoHsmPayload) {
  auto state = auth_block_state_;
  state.hsm_payload = brillo::Blob();

  TokenConsumerFuture token_future;
  service_.GenerateRecoveryRequest(
      kObfuscatedUsername, cryptorecovery::RequestMetadata{},
      epoch_response_blob_, state, token_future.GetCallback());

  auto token = token_future.Take();
  EXPECT_THAT(token, NotOk());
}

TEST_F(CryptohomeRecoveryAuthBlockServiceTest,
       GenerateRecoveryRequestNoChannelPubKey) {
  auto state = auth_block_state_;
  state.channel_pub_key = brillo::Blob();

  TokenConsumerFuture token_future;
  service_.GenerateRecoveryRequest(
      kObfuscatedUsername, cryptorecovery::RequestMetadata{},
      epoch_response_blob_, state, token_future.GetCallback());

  auto token = token_future.Take();
  EXPECT_THAT(token, NotOk());
}

TEST_F(CryptohomeRecoveryAuthBlockServiceTest,
       GenerateRecoveryRequestNoChannelPrivKey) {
  auto state = auth_block_state_;
  state.encrypted_channel_priv_key = brillo::Blob();

  TokenConsumerFuture token_future;
  service_.GenerateRecoveryRequest(
      kObfuscatedUsername, cryptorecovery::RequestMetadata{},
      epoch_response_blob_, state, token_future.GetCallback());

  auto token = token_future.Take();
  EXPECT_THAT(token, NotOk());
}

TEST_F(CryptohomeRecoveryAuthBlockServiceTest,
       GenerateRecoveryRequestNoEpochResponse) {
  TokenConsumerFuture token_future;
  service_.GenerateRecoveryRequest(
      kObfuscatedUsername, cryptorecovery::RequestMetadata{},
      /*epoch_response=*/brillo::Blob(), auth_block_state_,
      token_future.GetCallback());

  auto token = token_future.Take();
  EXPECT_THAT(token, NotOk());
}

}  // namespace
}  // namespace cryptohome
