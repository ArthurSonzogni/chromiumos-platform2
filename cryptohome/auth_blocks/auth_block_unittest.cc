// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_blocks/auth_block.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <base/files/file_path.h>
#include <gtest/gtest.h>
#include <libhwsec/factory/tpm2_simulator_factory_for_test.h>
#include <libhwsec/frontend/recovery_crypto/mock_frontend.h>
#include <libhwsec-foundation/crypto/aes.h>
#include <libhwsec-foundation/crypto/rsa.h>
#include <libhwsec-foundation/crypto/scrypt.h>
#include <libhwsec-foundation/error/testing_helper.h>

#include "cryptohome/auth_blocks/auth_block_utils.h"
#include "cryptohome/auth_blocks/cryptohome_recovery_auth_block.h"
#include "cryptohome/auth_blocks/scrypt_auth_block.h"
#include "cryptohome/auth_blocks/tpm_bound_to_pcr_auth_block.h"
#include "cryptohome/auth_blocks/tpm_not_bound_to_pcr_auth_block.h"
#include "cryptohome/crypto.h"
#include "cryptohome/crypto_error.h"
#include "cryptohome/cryptorecovery/fake_recovery_mediator_crypto.h"
#include "cryptohome/cryptorecovery/recovery_crypto_hsm_cbor_serialization.h"
#include "cryptohome/cryptorecovery/recovery_crypto_impl.h"
#include "cryptohome/cryptorecovery/recovery_crypto_util.h"
#include "cryptohome/fake_platform.h"
#include "cryptohome/flatbuffer_schemas/auth_block_state.h"
#include "cryptohome/mock_cryptohome_keys_manager.h"
#include "cryptohome/mock_le_credential_manager.h"
#include "cryptohome/vault_keyset.h"

namespace cryptohome {
namespace {

using cryptohome::cryptorecovery::FakeRecoveryMediatorCrypto;
using cryptohome::cryptorecovery::RecoveryCryptoImpl;
using cryptohome::error::CryptohomeError;
using cryptohome::error::CryptohomeLECredError;
using cryptohome::error::ErrorActionSet;
using cryptohome::error::PossibleAction;
using cryptohome::error::PrimaryAction;

using ::hwsec::TPMError;
using ::hwsec::TPMErrorBase;
using ::hwsec::TPMRetryAction;
using ::hwsec_foundation::DeriveSecretsScrypt;
using ::hwsec_foundation::kDefaultAesKeySize;
using ::hwsec_foundation::kDefaultPassBlobSize;
using ::hwsec_foundation::error::testing::ReturnError;
using ::hwsec_foundation::error::testing::ReturnValue;
using ::testing::_;
using ::testing::DoAll;
using ::testing::Exactly;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::SaveArg;
using ::testing::SetArgPointee;

constexpr char kFakeGaiaId[] = "123456789";
constexpr char kFakeDeviceId[] = "1234-5678-AAAA-BBBB";
constexpr char kUsername[] = "username@gmail.com";
constexpr char kObfuscatedUsername[] = "OBFUSCATED_USERNAME";

void SetupMockHwsec(NiceMock<hwsec::MockCryptohomeFrontend>& hwsec) {
  ON_CALL(hwsec, GetPubkeyHash(_))
      .WillByDefault(ReturnValue(brillo::BlobFromString("public key hash")));
  ON_CALL(hwsec, IsEnabled()).WillByDefault(ReturnValue(true));
  ON_CALL(hwsec, IsReady()).WillByDefault(ReturnValue(true));
}
}  // namespace

class CryptohomeRecoveryAuthBlockTest : public testing::Test {
 public:
  CryptohomeRecoveryAuthBlockTest()
      : ledger_info_(FakeRecoveryMediatorCrypto::GetLedgerInfo()) {}

  void SetUp() override {
    ASSERT_TRUE(FakeRecoveryMediatorCrypto::GetFakeMediatorPublicKey(
        &mediator_pub_key_));
    ASSERT_TRUE(
        FakeRecoveryMediatorCrypto::GetFakeEpochPublicKey(&epoch_pub_key_));
    ASSERT_TRUE(
        FakeRecoveryMediatorCrypto::GetFakeEpochResponse(&epoch_response_));
  }

  void PerformRecovery(
      hwsec::RecoveryCryptoFrontend* recovery_crypto,
      const CryptohomeRecoveryAuthBlockState& cryptohome_recovery_state,
      cryptorecovery::CryptoRecoveryRpcResponse* response_proto,
      brillo::SecureBlob* ephemeral_pub_key) {
    EXPECT_FALSE(cryptohome_recovery_state.hsm_payload.empty());
    EXPECT_FALSE(cryptohome_recovery_state.encrypted_destination_share.empty());
    EXPECT_FALSE(cryptohome_recovery_state.encrypted_channel_priv_key.empty());
    EXPECT_FALSE(cryptohome_recovery_state.channel_pub_key.empty());

    // Deserialize HSM payload stored on disk.
    cryptorecovery::HsmPayload hsm_payload;
    EXPECT_TRUE(DeserializeHsmPayloadFromCbor(
        cryptohome_recovery_state.hsm_payload, &hsm_payload));

    // Start recovery process.
    std::unique_ptr<cryptorecovery::RecoveryCryptoImpl> recovery =
        cryptorecovery::RecoveryCryptoImpl::Create(recovery_crypto, &platform_);
    ASSERT_TRUE(recovery);
    brillo::SecureBlob rsa_priv_key;

    cryptorecovery::RequestMetadata request_metadata;
    cryptorecovery::GenerateRecoveryRequestRequest
        generate_recovery_request_input_param(
            {.hsm_payload = hsm_payload,
             .request_meta_data = request_metadata,
             .epoch_response = epoch_response_,
             .encrypted_rsa_priv_key = rsa_priv_key,
             .encrypted_channel_priv_key =
                 cryptohome_recovery_state.encrypted_channel_priv_key,
             .channel_pub_key = cryptohome_recovery_state.channel_pub_key,
             .obfuscated_username = ObfuscatedUsername(kObfuscatedUsername)});
    cryptorecovery::CryptoRecoveryRpcRequest recovery_request;
    ASSERT_TRUE(recovery->GenerateRecoveryRequest(
        generate_recovery_request_input_param, &recovery_request,
        ephemeral_pub_key));

    // Simulate mediation (it will be done by Recovery Mediator service).
    std::unique_ptr<FakeRecoveryMediatorCrypto> mediator =
        FakeRecoveryMediatorCrypto::Create();
    ASSERT_TRUE(mediator);
    brillo::SecureBlob mediator_priv_key;
    ASSERT_TRUE(FakeRecoveryMediatorCrypto::GetFakeMediatorPrivateKey(
        &mediator_priv_key));
    brillo::SecureBlob epoch_priv_key;
    ASSERT_TRUE(
        FakeRecoveryMediatorCrypto::GetFakeEpochPrivateKey(&epoch_priv_key));

    ASSERT_TRUE(mediator->MediateRequestPayload(
        epoch_pub_key_, epoch_priv_key, mediator_priv_key, recovery_request,
        response_proto));
  }

  AuthInput GenerateFakeAuthInput() const {
    AuthInput auth_input;
    CryptohomeRecoveryAuthInput cryptohome_recovery_auth_input;
    cryptohome_recovery_auth_input.mediator_pub_key = mediator_pub_key_;
    cryptohome_recovery_auth_input.user_gaia_id = kFakeGaiaId;
    cryptohome_recovery_auth_input.device_user_id = kFakeDeviceId;
    auth_input.cryptohome_recovery_auth_input = cryptohome_recovery_auth_input;
    auth_input.obfuscated_username = ObfuscatedUsername(kObfuscatedUsername);
    auth_input.username = Username(kUsername);
    return auth_input;
  }

 protected:
  brillo::SecureBlob mediator_pub_key_;
  brillo::SecureBlob epoch_pub_key_;
  cryptorecovery::CryptoRecoveryEpochResponse epoch_response_;
  cryptorecovery::LedgerInfo ledger_info_;
  FakePlatform platform_;
};

TEST_F(CryptohomeRecoveryAuthBlockTest, SuccessTest) {
  AuthInput auth_input = GenerateFakeAuthInput();

  // IsPinWeaverEnabled()) returns `false` -> revocation is not supported.
  hwsec::Tpm2SimulatorFactoryForTest factory;
  std::unique_ptr<hwsec::RecoveryCryptoFrontend> recovery_crypto_fake_backend =
      factory.GetRecoveryCryptoFrontend();

  NiceMock<hwsec::MockCryptohomeFrontend> hwsec;
  SetupMockHwsec(hwsec);
  EXPECT_CALL(hwsec, IsPinWeaverEnabled()).WillRepeatedly(ReturnValue(false));

  KeyBlobs created_key_blobs;
  CryptohomeRecoveryAuthBlock auth_block(
      &hwsec, recovery_crypto_fake_backend.get(), nullptr, &platform_);
  AuthBlockState auth_state;
  EXPECT_TRUE(
      auth_block.Create(auth_input, &auth_state, &created_key_blobs).ok());
  ASSERT_TRUE(created_key_blobs.vkk_key.has_value());
  EXPECT_FALSE(auth_state.revocation_state.has_value());

  ASSERT_TRUE(std::holds_alternative<CryptohomeRecoveryAuthBlockState>(
      auth_state.state));
  const CryptohomeRecoveryAuthBlockState& cryptohome_recovery_state =
      std::get<CryptohomeRecoveryAuthBlockState>(auth_state.state);

  brillo::SecureBlob ephemeral_pub_key;
  cryptorecovery::CryptoRecoveryRpcResponse response_proto;
  PerformRecovery(recovery_crypto_fake_backend.get(), cryptohome_recovery_state,
                  &response_proto, &ephemeral_pub_key);

  CryptohomeRecoveryAuthInput derive_cryptohome_recovery_auth_input;
  // Save data required for key derivation in auth_input.
  std::string serialized_response_proto, serialized_epoch_response;
  EXPECT_TRUE(response_proto.SerializeToString(&serialized_response_proto));
  EXPECT_TRUE(epoch_response_.SerializeToString(&serialized_epoch_response));
  derive_cryptohome_recovery_auth_input.recovery_response =
      brillo::SecureBlob(serialized_response_proto);
  derive_cryptohome_recovery_auth_input.epoch_response =
      brillo::SecureBlob(serialized_epoch_response);
  derive_cryptohome_recovery_auth_input.ledger_name = ledger_info_.name;
  derive_cryptohome_recovery_auth_input.ledger_key_hash =
      ledger_info_.key_hash.value();
  derive_cryptohome_recovery_auth_input.ledger_public_key =
      ledger_info_.public_key.value();
  derive_cryptohome_recovery_auth_input.ephemeral_pub_key = ephemeral_pub_key;
  auth_input.cryptohome_recovery_auth_input =
      derive_cryptohome_recovery_auth_input;

  KeyBlobs derived_key_blobs;
  std::optional<AuthBlock::SuggestedAction> suggested_action;
  EXPECT_TRUE(
      auth_block
          .Derive(auth_input, auth_state, &derived_key_blobs, &suggested_action)
          .ok());
  ASSERT_TRUE(derived_key_blobs.vkk_key.has_value());

  // KeyBlobs generated by `Create` should be the same as KeyBlobs generated by
  // `Derive`.
  EXPECT_EQ(created_key_blobs.vkk_key, derived_key_blobs.vkk_key);
  EXPECT_EQ(created_key_blobs.vkk_iv, derived_key_blobs.vkk_iv);
  EXPECT_EQ(created_key_blobs.chaps_iv, derived_key_blobs.chaps_iv);
  EXPECT_EQ(suggested_action, std::nullopt);
}

TEST_F(CryptohomeRecoveryAuthBlockTest, SuccessTestWithRevocation) {
  AuthInput auth_input = GenerateFakeAuthInput();

  // IsPinWeaverEnabled() returns `true` -> revocation is supported.
  hwsec::Tpm2SimulatorFactoryForTest factory;
  std::unique_ptr<hwsec::RecoveryCryptoFrontend> recovery_crypto_fake_backend =
      factory.GetRecoveryCryptoFrontend();
  NiceMock<MockLECredentialManager> le_cred_manager;
  brillo::SecureBlob le_secret, he_secret;
  uint64_t le_label = 1;
  EXPECT_CALL(le_cred_manager, InsertCredential(_, _, _, _, _, _, _))
      .WillOnce(DoAll(SaveArg<1>(&le_secret), SaveArg<2>(&he_secret),
                      SetArgPointee<6>(le_label),
                      ReturnError<CryptohomeLECredError>()));

  NiceMock<hwsec::MockCryptohomeFrontend> hwsec;
  SetupMockHwsec(hwsec);
  EXPECT_CALL(hwsec, IsPinWeaverEnabled()).WillRepeatedly(ReturnValue(true));

  KeyBlobs created_key_blobs;
  CryptohomeRecoveryAuthBlock auth_block(
      &hwsec, recovery_crypto_fake_backend.get(), &le_cred_manager, &platform_);
  AuthBlockState auth_state;
  EXPECT_TRUE(
      auth_block.Create(auth_input, &auth_state, &created_key_blobs).ok());
  ASSERT_TRUE(created_key_blobs.vkk_key.has_value());

  // The revocation state should be created with the le_label returned by
  // InsertCredential().
  ASSERT_TRUE(auth_state.revocation_state.has_value());
  EXPECT_EQ(le_label, auth_state.revocation_state.value().le_label);
  EXPECT_FALSE(he_secret.empty());

  ASSERT_TRUE(std::holds_alternative<CryptohomeRecoveryAuthBlockState>(
      auth_state.state));
  const CryptohomeRecoveryAuthBlockState& cryptohome_recovery_state =
      std::get<CryptohomeRecoveryAuthBlockState>(auth_state.state);

  brillo::SecureBlob ephemeral_pub_key;
  cryptorecovery::CryptoRecoveryRpcResponse response_proto;
  PerformRecovery(recovery_crypto_fake_backend.get(), cryptohome_recovery_state,
                  &response_proto, &ephemeral_pub_key);

  CryptohomeRecoveryAuthInput derive_cryptohome_recovery_auth_input;
  // Save data required for key derivation in auth_input.
  std::string serialized_response_proto, serialized_epoch_response;
  EXPECT_TRUE(response_proto.SerializeToString(&serialized_response_proto));
  EXPECT_TRUE(epoch_response_.SerializeToString(&serialized_epoch_response));
  derive_cryptohome_recovery_auth_input.recovery_response =
      brillo::SecureBlob(serialized_response_proto);
  derive_cryptohome_recovery_auth_input.epoch_response =
      brillo::SecureBlob(serialized_epoch_response);
  derive_cryptohome_recovery_auth_input.ephemeral_pub_key = ephemeral_pub_key;
  derive_cryptohome_recovery_auth_input.ledger_name = ledger_info_.name;
  derive_cryptohome_recovery_auth_input.ledger_key_hash =
      ledger_info_.key_hash.value();
  derive_cryptohome_recovery_auth_input.ledger_public_key =
      ledger_info_.public_key.value();
  auth_input.cryptohome_recovery_auth_input =
      derive_cryptohome_recovery_auth_input;

  brillo::SecureBlob le_secret_1;
  EXPECT_CALL(le_cred_manager, CheckCredential(le_label, _, _, _))
      .WillOnce(DoAll(SaveArg<1>(&le_secret_1), SetArgPointee<2>(he_secret),
                      ReturnError<CryptohomeLECredError>()));
  KeyBlobs derived_key_blobs;
  std::optional<AuthBlock::SuggestedAction> suggested_action;
  EXPECT_TRUE(
      auth_block
          .Derive(auth_input, auth_state, &derived_key_blobs, &suggested_action)
          .ok());
  ASSERT_TRUE(derived_key_blobs.vkk_key.has_value());

  // LE secret should be the same in InsertCredential and CheckCredential.
  EXPECT_EQ(le_secret, le_secret_1);

  // KeyBlobs generated by `Create` should be the same as KeyBlobs generated by
  // `Derive`.
  EXPECT_EQ(created_key_blobs.vkk_key, derived_key_blobs.vkk_key);
  EXPECT_EQ(created_key_blobs.vkk_iv, derived_key_blobs.vkk_iv);
  EXPECT_EQ(created_key_blobs.chaps_iv, derived_key_blobs.chaps_iv);
  EXPECT_EQ(suggested_action, std::nullopt);
}

TEST_F(CryptohomeRecoveryAuthBlockTest, CreateGeneratesRecoveryId) {
  AuthInput auth_input = GenerateFakeAuthInput();

  // IsPinWeaverEnabled()) returns `false` -> revocation is not supported.
  hwsec::Tpm2SimulatorFactoryForTest factory;
  std::unique_ptr<hwsec::RecoveryCryptoFrontend> recovery_crypto_fake_backend =
      factory.GetRecoveryCryptoFrontend();

  NiceMock<hwsec::MockCryptohomeFrontend> hwsec;
  SetupMockHwsec(hwsec);
  EXPECT_CALL(hwsec, IsPinWeaverEnabled()).WillRepeatedly(ReturnValue(false));

  KeyBlobs created_key_blobs;
  CryptohomeRecoveryAuthBlock auth_block(
      &hwsec, recovery_crypto_fake_backend.get(), nullptr, &platform_);
  AuthBlockState auth_state;
  EXPECT_TRUE(
      auth_block.Create(auth_input, &auth_state, &created_key_blobs).ok());
  ASSERT_TRUE(created_key_blobs.vkk_key.has_value());
  EXPECT_FALSE(auth_state.revocation_state.has_value());

  std::unique_ptr<cryptorecovery::RecoveryCryptoImpl> recovery =
      cryptorecovery::RecoveryCryptoImpl::Create(
          recovery_crypto_fake_backend.get(), &platform_);
  AccountIdentifier account_id;
  account_id.set_email(kUsername);
  std::vector<std::string> recovery_ids =
      recovery->GetLastRecoveryIds(account_id, 10);
  ASSERT_EQ(recovery_ids.size(), 1);
  EXPECT_FALSE(recovery_ids[0].empty());
}

TEST_F(CryptohomeRecoveryAuthBlockTest, MissingObfuscatedUsername) {
  AuthInput auth_input = GenerateFakeAuthInput();
  auth_input.obfuscated_username.reset();

  // Tpm::GetLECredentialBackend() returns `nullptr` -> revocation is not
  // supported.
  hwsec::Tpm2SimulatorFactoryForTest factory;
  std::unique_ptr<hwsec::RecoveryCryptoFrontend> recovery_crypto_fake_backend =
      factory.GetRecoveryCryptoFrontend();

  NiceMock<hwsec::MockCryptohomeFrontend> hwsec;
  SetupMockHwsec(hwsec);

  KeyBlobs created_key_blobs;
  CryptohomeRecoveryAuthBlock auth_block(
      &hwsec, recovery_crypto_fake_backend.get(),
      /*LECredentialManager*=*/nullptr, &platform_);
  AuthBlockState auth_state;
  EXPECT_FALSE(
      auth_block.Create(auth_input, &auth_state, &created_key_blobs).ok());
  EXPECT_FALSE(created_key_blobs.vkk_key.has_value());
  EXPECT_FALSE(created_key_blobs.vkk_iv.has_value());
  EXPECT_FALSE(created_key_blobs.chaps_iv.has_value());
  EXPECT_FALSE(auth_state.revocation_state.has_value());
}

}  // namespace cryptohome
