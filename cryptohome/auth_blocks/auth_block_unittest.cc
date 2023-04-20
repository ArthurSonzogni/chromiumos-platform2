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
#include "cryptohome/auth_blocks/tpm_ecc_auth_block.h"
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

TpmEccAuthBlockState GetDefaultEccAuthBlockState() {
  TpmEccAuthBlockState auth_block_state;
  auth_block_state.salt = brillo::SecureBlob(32, 'A');
  auth_block_state.vkk_iv = brillo::SecureBlob(32, 'B');
  auth_block_state.sealed_hvkkm = brillo::SecureBlob(32, 'C');
  auth_block_state.extended_sealed_hvkkm = brillo::SecureBlob(32, 'D');
  auth_block_state.auth_value_rounds = 5;
  return auth_block_state;
}

void SetupMockHwsec(NiceMock<hwsec::MockCryptohomeFrontend>& hwsec) {
  ON_CALL(hwsec, GetPubkeyHash(_))
      .WillByDefault(ReturnValue(brillo::BlobFromString("public key hash")));
  ON_CALL(hwsec, IsEnabled()).WillByDefault(ReturnValue(true));
  ON_CALL(hwsec, IsReady()).WillByDefault(ReturnValue(true));
}
}  // namespace

TEST(TpmBoundToPcrTest, CreateTest) {
  // Set up inputs to the test.
  brillo::SecureBlob vault_key(20, 'C');
  SerializedVaultKeyset serialized;

  // Set up the mock expectations.
  brillo::SecureBlob scrypt_derived_key;
  NiceMock<hwsec::MockCryptohomeFrontend> hwsec;
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;
  brillo::SecureBlob auth_value(256, 'a');

  SetupMockHwsec(hwsec);

  EXPECT_CALL(hwsec, GetAuthValue(_, _))
      .WillOnce(
          DoAll(SaveArg<1>(&scrypt_derived_key), ReturnValue(auth_value)));
  EXPECT_CALL(hwsec, SealWithCurrentUser(_, auth_value, _)).Times(Exactly(2));
  ON_CALL(hwsec, SealWithCurrentUser(_, _, _))
      .WillByDefault(ReturnValue(brillo::Blob()));

  AuthInput user_input = {vault_key,
                          /*locked_to_single_user=*/std::nullopt, Username(),
                          ObfuscatedUsername(kObfuscatedUsername),
                          /*reset_secret=*/std::nullopt};
  KeyBlobs vkk_data;

  TpmBoundToPcrAuthBlock auth_block(&hwsec, &cryptohome_keys_manager);
  AuthBlockState auth_state;
  EXPECT_TRUE(auth_block.Create(user_input, &auth_state, &vkk_data).ok());
  EXPECT_TRUE(
      std::holds_alternative<TpmBoundToPcrAuthBlockState>(auth_state.state));

  EXPECT_NE(vkk_data.vkk_key, std::nullopt);
  EXPECT_NE(vkk_data.vkk_iv, std::nullopt);
  EXPECT_NE(vkk_data.chaps_iv, std::nullopt);

  auto& tpm_state = std::get<TpmBoundToPcrAuthBlockState>(auth_state.state);

  EXPECT_TRUE(tpm_state.salt.has_value());
  const brillo::SecureBlob& salt = tpm_state.salt.value();
  brillo::SecureBlob scrypt_derived_key_result(kDefaultPassBlobSize);
  EXPECT_TRUE(
      DeriveSecretsScrypt(vault_key, salt, {&scrypt_derived_key_result}));
  EXPECT_EQ(scrypt_derived_key, scrypt_derived_key_result);
}

TEST(TpmBoundToPcrTest, CreateFailTpm) {
  // Set up inputs to the test.
  brillo::SecureBlob vault_key(20, 'C');
  SerializedVaultKeyset serialized;

  // Set up the mock expectations.
  NiceMock<hwsec::MockCryptohomeFrontend> hwsec;
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;

  brillo::SecureBlob auth_value(256, 'a');

  SetupMockHwsec(hwsec);

  EXPECT_CALL(hwsec, GetAuthValue(_, _))
      .WillOnce(DoAll(ReturnValue(brillo::Blob())));

  ON_CALL(hwsec, SealWithCurrentUser(_, _, _))
      .WillByDefault(ReturnError<TPMError>("fake", TPMRetryAction::kNoRetry));

  AuthInput user_input = {vault_key,
                          /*locked_to_single_user=*/std::nullopt, Username(),
                          ObfuscatedUsername(kObfuscatedUsername),
                          /*reset_secret=*/std::nullopt};
  KeyBlobs vkk_data;
  TpmBoundToPcrAuthBlock auth_block(&hwsec, &cryptohome_keys_manager);
  AuthBlockState auth_state;
  EXPECT_EQ(CryptoError::CE_TPM_CRYPTO,
            auth_block.Create(user_input, &auth_state, &vkk_data)
                ->local_crypto_error());
}

// Test the Create operation fails when there's no user_input provided.
TEST(TpmBoundToPcrTest, CreateFailNoUserInput) {
  // Prepare.
  NiceMock<hwsec::MockCryptohomeFrontend> hwsec;
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;
  TpmBoundToPcrAuthBlock auth_block(&hwsec, &cryptohome_keys_manager);
  AuthInput auth_input = {.obfuscated_username =
                              ObfuscatedUsername(kObfuscatedUsername)};

  // Test.
  AuthBlockState auth_state;
  KeyBlobs vkk_data;
  EXPECT_EQ(CryptoError::CE_OTHER_CRYPTO,
            auth_block.Create(auth_input, &auth_state, &vkk_data)
                ->local_crypto_error());
}

// Test the Create operation fails when there's no obfuscated_username provided.
TEST(TpmBoundToPcrTest, CreateFailNoObfuscated) {
  // Prepare.
  brillo::SecureBlob user_input(20, 'C');
  NiceMock<hwsec::MockCryptohomeFrontend> hwsec;
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;
  TpmBoundToPcrAuthBlock auth_block(&hwsec, &cryptohome_keys_manager);
  AuthInput auth_input = {.user_input = user_input};

  // Test.
  AuthBlockState auth_state;
  KeyBlobs vkk_data;
  EXPECT_EQ(CryptoError::CE_OTHER_CRYPTO,
            auth_block.Create(auth_input, &auth_state, &vkk_data)
                ->local_crypto_error());
}

TEST(TPMAuthBlockTest, DecryptBoundToPcrTest) {
  brillo::SecureBlob vault_key(20, 'C');
  brillo::SecureBlob tpm_key(20, 'B');
  brillo::SecureBlob salt(PKCS5_SALT_LEN, 'A');

  brillo::SecureBlob vkk_iv(kDefaultAesKeySize);
  brillo::SecureBlob vkk_key;

  brillo::SecureBlob pass_blob(kDefaultPassBlobSize);
  ASSERT_TRUE(DeriveSecretsScrypt(vault_key, salt, {&pass_blob}));

  NiceMock<hwsec::MockCryptohomeFrontend> hwsec;
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;

  SetupMockHwsec(hwsec);

  EXPECT_CALL(hwsec, PreloadSealedData(_)).WillOnce(Invoke([&](auto&&) {
    return hwsec::ScopedKey(hwsec::Key{.token = 5566},
                            hwsec.GetFakeMiddlewareDerivative());
  }));
  brillo::SecureBlob auth_value(256, 'a');
  EXPECT_CALL(hwsec, GetAuthValue(_, pass_blob))
      .WillOnce(ReturnValue(auth_value));
  EXPECT_CALL(hwsec, UnsealWithCurrentUser(_, auth_value, _))
      .WillOnce([](std::optional<hwsec::Key> preload_data, auto&&, auto&&) {
        EXPECT_TRUE(preload_data.has_value());
        EXPECT_EQ(preload_data->token, 5566);
        return brillo::SecureBlob();
      });

  TpmBoundToPcrAuthBlock tpm_auth_block(&hwsec, &cryptohome_keys_manager);
  EXPECT_TRUE(
      tpm_auth_block
          .DecryptTpmBoundToPcr(vault_key, tpm_key, salt, &vkk_iv, &vkk_key)
          .ok());
}

TEST(TPMAuthBlockTest, DecryptBoundToPcrNoPreloadTest) {
  brillo::SecureBlob vault_key(20, 'C');
  brillo::SecureBlob tpm_key(20, 'B');
  brillo::SecureBlob salt(PKCS5_SALT_LEN, 'A');

  brillo::SecureBlob vkk_iv(kDefaultAesKeySize);
  brillo::SecureBlob vkk_key;

  brillo::SecureBlob pass_blob(kDefaultPassBlobSize);
  ASSERT_TRUE(DeriveSecretsScrypt(vault_key, salt, {&pass_blob}));

  NiceMock<hwsec::MockCryptohomeFrontend> hwsec;
  SetupMockHwsec(hwsec);
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;
  EXPECT_CALL(hwsec, PreloadSealedData(_)).WillOnce(ReturnValue(std::nullopt));
  brillo::SecureBlob auth_value(256, 'a');
  EXPECT_CALL(hwsec, GetAuthValue(_, pass_blob))
      .WillOnce(ReturnValue(auth_value));
  EXPECT_CALL(hwsec, UnsealWithCurrentUser(_, auth_value, _))
      .WillOnce([](std::optional<hwsec::Key> preload_data, auto&&, auto&&) {
        EXPECT_FALSE(preload_data.has_value());
        return brillo::SecureBlob();
      });

  TpmBoundToPcrAuthBlock tpm_auth_block(&hwsec, &cryptohome_keys_manager);
  EXPECT_TRUE(
      tpm_auth_block
          .DecryptTpmBoundToPcr(vault_key, tpm_key, salt, &vkk_iv, &vkk_key)
          .ok());
}

TEST(TPMAuthBlockTest, DecryptBoundToPcrPreloadFailedTest) {
  brillo::SecureBlob vault_key(20, 'C');
  brillo::SecureBlob tpm_key(20, 'B');
  brillo::SecureBlob salt(PKCS5_SALT_LEN, 'A');

  brillo::SecureBlob vkk_iv(kDefaultAesKeySize);
  brillo::SecureBlob vkk_key;

  brillo::SecureBlob pass_blob(kDefaultPassBlobSize);
  ASSERT_TRUE(DeriveSecretsScrypt(vault_key, salt, {&pass_blob}));

  NiceMock<hwsec::MockCryptohomeFrontend> hwsec;
  SetupMockHwsec(hwsec);
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;
  EXPECT_CALL(hwsec, PreloadSealedData(_))
      .WillOnce(ReturnError<TPMError>("fake", TPMRetryAction::kNoRetry));

  TpmBoundToPcrAuthBlock tpm_auth_block(&hwsec, &cryptohome_keys_manager);
  EXPECT_FALSE(
      tpm_auth_block
          .DecryptTpmBoundToPcr(vault_key, tpm_key, salt, &vkk_iv, &vkk_key)
          .ok());
}

TEST(TpmAuthBlockTest, DeriveTest) {
  SerializedVaultKeyset serialized;
  serialized.set_flags(SerializedVaultKeyset::TPM_WRAPPED |
                       SerializedVaultKeyset::PCR_BOUND |
                       SerializedVaultKeyset::SCRYPT_DERIVED);

  brillo::SecureBlob key(20, 'B');
  brillo::SecureBlob tpm_key(20, 'C');
  std::string salt(PKCS5_SALT_LEN, 'A');

  serialized.set_salt(salt);
  serialized.set_tpm_key(tpm_key.data(), tpm_key.size());
  serialized.set_extended_tpm_key(tpm_key.data(), tpm_key.size());

  // Make sure TpmAuthBlock calls DecryptTpmBoundToPcr in this case.
  NiceMock<hwsec::MockCryptohomeFrontend> hwsec;
  SetupMockHwsec(hwsec);
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;
  EXPECT_CALL(hwsec, PreloadSealedData(_)).WillOnce(ReturnValue(std::nullopt));
  EXPECT_CALL(hwsec, GetAuthValue(_, _))
      .WillOnce(ReturnValue(brillo::SecureBlob()));
  EXPECT_CALL(hwsec, UnsealWithCurrentUser(_, _, _))
      .WillOnce(ReturnValue(brillo::SecureBlob()));

  TpmBoundToPcrAuthBlock auth_block(&hwsec, &cryptohome_keys_manager);

  KeyBlobs key_out_data;
  std::optional<AuthBlock::SuggestedAction> suggested_action;
  AuthInput auth_input;
  auth_input.user_input = key;
  auth_input.locked_to_single_user = false;

  VaultKeyset vk;
  vk.InitializeFromSerialized(serialized);
  AuthBlockState auth_state;
  EXPECT_TRUE(GetAuthBlockState(vk, auth_state));

  EXPECT_TRUE(
      auth_block
          .Derive(auth_input, auth_state, &key_out_data, &suggested_action)
          .ok());

  // Assert that the returned key blobs isn't uninitialized.
  EXPECT_NE(key_out_data.vkk_iv, std::nullopt);
  EXPECT_NE(key_out_data.vkk_key, std::nullopt);
  EXPECT_EQ(key_out_data.vkk_iv.value(), key_out_data.chaps_iv.value());
  EXPECT_EQ(suggested_action, std::nullopt);
}

// Test TpmBoundToPcrAuthBlock derive fails when there's no user_input provided.
TEST(TpmAuthBlockTest, DeriveFailureNoUserInput) {
  brillo::SecureBlob tpm_key(20, 'C');
  brillo::SecureBlob salt(PKCS5_SALT_LEN, 'A');
  NiceMock<hwsec::MockCryptohomeFrontend> hwsec;
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;
  TpmBoundToPcrAuthBlock auth_block(&hwsec, &cryptohome_keys_manager);

  AuthBlockState auth_state;
  TpmBoundToPcrAuthBlockState state;
  state.scrypt_derived = true;
  state.salt = salt;
  state.tpm_key = tpm_key;
  state.extended_tpm_key = tpm_key;
  auth_state.state = std::move(state);

  AuthInput auth_input = {};
  KeyBlobs key_blobs;
  std::optional<AuthBlock::SuggestedAction> suggested_action;
  EXPECT_EQ(
      CryptoError::CE_OTHER_CRYPTO,
      auth_block.Derive(auth_input, auth_state, &key_blobs, &suggested_action)
          ->local_crypto_error());
}

// Check required field |salt| in TpmBoundToPcrAuthBlockState.
TEST(TpmAuthBlockTest, DeriveFailureMissingSalt) {
  brillo::SecureBlob tpm_key(20, 'C');
  brillo::SecureBlob user_input("foo");
  NiceMock<hwsec::MockCryptohomeFrontend> hwsec;
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;
  TpmBoundToPcrAuthBlock auth_block(&hwsec, &cryptohome_keys_manager);

  AuthBlockState auth_state;
  TpmBoundToPcrAuthBlockState state;
  state.scrypt_derived = true;
  state.tpm_key = tpm_key;
  state.extended_tpm_key = tpm_key;
  auth_state.state = std::move(state);

  KeyBlobs key_blobs;
  std::optional<AuthBlock::SuggestedAction> suggested_action;
  AuthInput auth_input = {.user_input = user_input};
  EXPECT_EQ(
      CryptoError::CE_OTHER_CRYPTO,
      auth_block.Derive(auth_input, auth_state, &key_blobs, &suggested_action)
          ->local_crypto_error());
}

// Check required field |tpm_key| in TpmBoundToPcrAuthBlockState.
TEST(TpmAuthBlockTest, DeriveFailureMissingTpmKey) {
  brillo::SecureBlob tpm_key(20, 'C');
  brillo::SecureBlob salt(PKCS5_SALT_LEN, 'A');
  brillo::SecureBlob user_input("foo");
  NiceMock<hwsec::MockCryptohomeFrontend> hwsec;
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;
  TpmBoundToPcrAuthBlock auth_block(&hwsec, &cryptohome_keys_manager);

  AuthBlockState auth_state;
  TpmBoundToPcrAuthBlockState state;
  state.scrypt_derived = true;
  state.salt = salt;
  state.extended_tpm_key = tpm_key;
  auth_state.state = std::move(state);

  KeyBlobs key_blobs;
  std::optional<AuthBlock::SuggestedAction> suggested_action;
  AuthInput auth_input = {.user_input = user_input};
  EXPECT_EQ(
      CryptoError::CE_OTHER_CRYPTO,
      auth_block.Derive(auth_input, auth_state, &key_blobs, &suggested_action)
          ->local_crypto_error());
}

// Check required field |extended_tpm_key| in TpmBoundToPcrAuthBlockState.
TEST(TpmAuthBlockTest, DeriveFailureMissingExtendedTpmKey) {
  brillo::SecureBlob tpm_key(20, 'C');
  brillo::SecureBlob salt(PKCS5_SALT_LEN, 'A');
  brillo::SecureBlob user_input("foo");
  NiceMock<hwsec::MockCryptohomeFrontend> hwsec;
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;
  TpmBoundToPcrAuthBlock auth_block(&hwsec, &cryptohome_keys_manager);

  AuthBlockState auth_state;
  TpmBoundToPcrAuthBlockState state;
  state.scrypt_derived = true;
  state.salt = salt;
  state.tpm_key = tpm_key;
  auth_state.state = std::move(state);

  KeyBlobs key_blobs;
  std::optional<AuthBlock::SuggestedAction> suggested_action;
  AuthInput auth_input = {.user_input = user_input};
  EXPECT_EQ(
      CryptoError::CE_OTHER_CRYPTO,
      auth_block.Derive(auth_input, auth_state, &key_blobs, &suggested_action)
          ->local_crypto_error());
}

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

// Test the TpmEccAuthBlock::Create works correctly.
TEST(TpmEccAuthBlockTest, CreateTest) {
  // Set up inputs to the test.
  brillo::SecureBlob vault_key(20, 'C');

  // Set up the mock expectations.
  brillo::SecureBlob scrypt_derived_key;
  NiceMock<hwsec::MockCryptohomeFrontend> hwsec;
  SetupMockHwsec(hwsec);
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;
  brillo::SecureBlob auth_value(32, 'a');
  EXPECT_CALL(hwsec, GetManufacturer()).WillOnce(ReturnValue(0x43524f53));
  EXPECT_CALL(hwsec, GetAuthValue(_, _))
      .Times(Exactly(5))
      .WillOnce(DoAll(SaveArg<1>(&scrypt_derived_key), ReturnValue(auth_value)))
      .WillRepeatedly(ReturnValue(auth_value));
  EXPECT_CALL(hwsec, SealWithCurrentUser(_, auth_value, _))
      .WillOnce(ReturnValue(brillo::Blob()))
      .WillOnce(ReturnValue(brillo::Blob()));

  AuthInput user_input = {vault_key,
                          /*locked_to_single_user=*/std::nullopt, Username(),
                          ObfuscatedUsername(kObfuscatedUsername),
                          /*reset_secret=*/std::nullopt};
  KeyBlobs vkk_data;

  TpmEccAuthBlock auth_block(&hwsec, &cryptohome_keys_manager);
  AuthBlockState auth_state;
  EXPECT_TRUE(auth_block.Create(user_input, &auth_state, &vkk_data).ok());
  EXPECT_TRUE(std::holds_alternative<TpmEccAuthBlockState>(auth_state.state));

  EXPECT_NE(vkk_data.vkk_key, std::nullopt);
  EXPECT_NE(vkk_data.vkk_iv, std::nullopt);
  EXPECT_NE(vkk_data.chaps_iv, std::nullopt);

  auto& tpm_state = std::get<TpmEccAuthBlockState>(auth_state.state);

  EXPECT_TRUE(tpm_state.salt.has_value());
  const brillo::SecureBlob& salt = tpm_state.salt.value();
  brillo::SecureBlob scrypt_derived_key_result(kDefaultPassBlobSize);
  EXPECT_TRUE(
      DeriveSecretsScrypt(vault_key, salt, {&scrypt_derived_key_result}));
  EXPECT_EQ(scrypt_derived_key, scrypt_derived_key_result);
}

// Test the retry function of TpmEccAuthBlock::Create works correctly.
TEST(TpmEccAuthBlockTest, CreateRetryTest) {
  // Set up inputs to the test.
  brillo::SecureBlob vault_key(20, 'C');

  // Set up the mock expectations.
  brillo::SecureBlob scrypt_derived_key;
  NiceMock<hwsec::MockCryptohomeFrontend> hwsec;
  SetupMockHwsec(hwsec);
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;
  brillo::SecureBlob auth_value(32, 'a');
  EXPECT_CALL(hwsec, GetManufacturer())
      .Times(Exactly(2))
      .WillRepeatedly(ReturnValue(0x43524f53));

  // Add some communication errors and retry errors that may come from TPM
  // daemon.
  EXPECT_CALL(hwsec, GetAuthValue(_, _))
      .Times(Exactly(6))
      .WillOnce(
          ReturnError<TPMError>("ECC scalar out of range",
                                TPMRetryAction::kEllipticCurveScalarOutOfRange))
      .WillOnce(DoAll(SaveArg<1>(&scrypt_derived_key), ReturnValue(auth_value)))
      .WillRepeatedly(ReturnValue(auth_value));

  // Add some communication errors that may come from TPM daemon.
  EXPECT_CALL(hwsec, SealWithCurrentUser(_, auth_value, _))
      .WillOnce(ReturnValue(brillo::Blob()))
      .WillOnce(ReturnValue(brillo::Blob()));

  AuthInput user_input = {vault_key,
                          /*locked_to_single_user=*/std::nullopt, Username(),
                          ObfuscatedUsername(kObfuscatedUsername),
                          /*reset_secret=*/std::nullopt};
  KeyBlobs vkk_data;

  TpmEccAuthBlock auth_block(&hwsec, &cryptohome_keys_manager);
  AuthBlockState auth_state;
  EXPECT_TRUE(auth_block.Create(user_input, &auth_state, &vkk_data).ok());
  EXPECT_TRUE(std::holds_alternative<TpmEccAuthBlockState>(auth_state.state));

  EXPECT_NE(vkk_data.vkk_key, std::nullopt);
  EXPECT_NE(vkk_data.vkk_iv, std::nullopt);
  EXPECT_NE(vkk_data.chaps_iv, std::nullopt);

  auto& tpm_state = std::get<TpmEccAuthBlockState>(auth_state.state);

  EXPECT_TRUE(tpm_state.salt.has_value());
  const brillo::SecureBlob& salt = tpm_state.salt.value();
  brillo::SecureBlob scrypt_derived_key_result(kDefaultPassBlobSize);
  EXPECT_TRUE(
      DeriveSecretsScrypt(vault_key, salt, {&scrypt_derived_key_result}));
  EXPECT_EQ(scrypt_derived_key, scrypt_derived_key_result);
}

// Test the retry function of TpmEccAuthBlock::Create failed as expected.
TEST(TpmEccAuthBlockTest, CreateRetryFailTest) {
  // Set up inputs to the test.
  brillo::SecureBlob vault_key(20, 'C');

  // Set up the mock expectations.
  brillo::SecureBlob scrypt_derived_key;
  NiceMock<hwsec::MockCryptohomeFrontend> hwsec;
  SetupMockHwsec(hwsec);
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;
  brillo::SecureBlob auth_value(32, 'a');
  EXPECT_CALL(hwsec, GetManufacturer()).WillRepeatedly(ReturnValue(0x43524f53));
  // The TpmEccAuthBlock shouldn't retry forever if the TPM always returning
  // error.
  EXPECT_CALL(hwsec, GetAuthValue(_, _))
      .WillRepeatedly(ReturnError<TPMError>("reboot", TPMRetryAction::kReboot));

  AuthInput user_input = {vault_key,
                          /*locked_to_single_user=*/std::nullopt, Username(),
                          ObfuscatedUsername(kObfuscatedUsername),
                          /*reset_secret=*/std::nullopt};
  KeyBlobs vkk_data;
  TpmEccAuthBlock auth_block(&hwsec, &cryptohome_keys_manager);
  AuthBlockState auth_state;
  EXPECT_EQ(CryptoError::CE_TPM_REBOOT,
            auth_block.Create(user_input, &auth_state, &vkk_data)
                ->local_crypto_error());
}

// Test the Create operation fails when there's no user_input provided.
TEST(TpmEccAuthBlockTest, CreateFailNoUserInput) {
  // Prepare.
  NiceMock<hwsec::MockCryptohomeFrontend> hwsec;
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;
  TpmEccAuthBlock auth_block(&hwsec, &cryptohome_keys_manager);
  AuthInput auth_input = {.obfuscated_username =
                              ObfuscatedUsername(kObfuscatedUsername)};

  // Test.
  AuthBlockState auth_state;
  KeyBlobs vkk_data;
  EXPECT_EQ(auth_block.Create(auth_input, &auth_state, &vkk_data)
                ->local_crypto_error(),
            CryptoError::CE_OTHER_CRYPTO);
}

// Test the Create operation fails when there's no obfuscated_username provided.
TEST(TpmEccAuthBlockTest, CreateFailNoObfuscated) {
  // Prepare.
  brillo::SecureBlob user_input(20, 'C');
  NiceMock<hwsec::MockCryptohomeFrontend> hwsec;
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;
  TpmBoundToPcrAuthBlock auth_block(&hwsec, &cryptohome_keys_manager);
  AuthInput auth_input = {.user_input = user_input};

  // Test.
  AuthBlockState auth_state;
  KeyBlobs vkk_data;
  EXPECT_EQ(auth_block.Create(auth_input, &auth_state, &vkk_data)
                ->local_crypto_error(),
            CryptoError::CE_OTHER_CRYPTO);
}

// Test SealToPcr in TpmEccAuthBlock::Create failed as expected.
TEST(TpmEccAuthBlockTest, CreateSealToPcrFailTest) {
  // Set up inputs to the test.
  brillo::SecureBlob vault_key(20, 'C');

  // Set up the mock expectations.
  NiceMock<hwsec::MockCryptohomeFrontend> hwsec;
  SetupMockHwsec(hwsec);
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;
  brillo::SecureBlob auth_value(32, 'a');
  EXPECT_CALL(hwsec, GetManufacturer()).WillOnce(ReturnValue(0x49465800));
  EXPECT_CALL(hwsec, GetAuthValue(_, _))
      .Times(2)
      .WillRepeatedly(ReturnValue(auth_value));

  EXPECT_CALL(hwsec, SealWithCurrentUser(_, auth_value, _))
      .WillOnce(ReturnError<TPMError>("fake", TPMRetryAction::kNoRetry));

  AuthInput user_input = {vault_key,
                          /*locked_to_single_user=*/std::nullopt, Username(),
                          ObfuscatedUsername(kObfuscatedUsername),
                          /*reset_secret=*/std::nullopt};
  KeyBlobs vkk_data;
  TpmEccAuthBlock auth_block(&hwsec, &cryptohome_keys_manager);
  AuthBlockState auth_state;
  EXPECT_EQ(CryptoError::CE_TPM_CRYPTO,
            auth_block.Create(user_input, &auth_state, &vkk_data)
                ->local_crypto_error());
}

// Test second SealToPcr in TpmEccAuthBlock::Create failed as expected.
TEST(TpmEccAuthBlockTest, CreateSecondSealToPcrFailTest) {
  // Set up inputs to the test.
  brillo::SecureBlob vault_key(20, 'C');

  // Set up the mock expectations.
  NiceMock<hwsec::MockCryptohomeFrontend> hwsec;
  SetupMockHwsec(hwsec);
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;
  brillo::SecureBlob auth_value(32, 'a');
  EXPECT_CALL(hwsec, GetManufacturer()).WillOnce(ReturnValue(0x49465800));
  EXPECT_CALL(hwsec, GetAuthValue(_, _))
      .Times(2)
      .WillRepeatedly(ReturnValue(auth_value));

  EXPECT_CALL(hwsec, SealWithCurrentUser(_, auth_value, _))
      .WillOnce(ReturnValue(brillo::Blob()))
      .WillOnce(ReturnError<TPMError>("fake", TPMRetryAction::kNoRetry));

  AuthInput user_input = {vault_key,
                          /*locked_to_single_user=*/std::nullopt,
                          Username(kObfuscatedUsername), ObfuscatedUsername(),
                          /*reset_secret=*/std::nullopt};
  KeyBlobs vkk_data;
  TpmEccAuthBlock auth_block(&hwsec, &cryptohome_keys_manager);
  AuthBlockState auth_state;
  EXPECT_EQ(CryptoError::CE_TPM_CRYPTO,
            auth_block.Create(user_input, &auth_state, &vkk_data)
                ->local_crypto_error());
}

// Test GetEccAuthValue in TpmEccAuthBlock::Create failed as expected.
TEST(TpmEccAuthBlockTest, CreateEccAuthValueFailTest) {
  // Set up inputs to the test.
  brillo::SecureBlob vault_key(20, 'C');

  // Set up the mock expectations.
  NiceMock<hwsec::MockCryptohomeFrontend> hwsec;
  SetupMockHwsec(hwsec);
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;
  brillo::SecureBlob auth_value(32, 'a');

  EXPECT_CALL(hwsec, GetManufacturer())
      .WillOnce(ReturnError<TPMError>("fake", TPMRetryAction::kNoRetry));
  EXPECT_CALL(hwsec, GetAuthValue(_, _))
      .WillOnce(ReturnError<TPMError>("fake", TPMRetryAction::kNoRetry));

  AuthInput user_input = {vault_key,
                          /*locked_to_single_user=*/std::nullopt, Username(),
                          ObfuscatedUsername(kObfuscatedUsername),
                          /*reset_secret=*/std::nullopt};
  KeyBlobs vkk_data;
  TpmEccAuthBlock auth_block(&hwsec, &cryptohome_keys_manager);
  AuthBlockState auth_state;
  EXPECT_EQ(CryptoError::CE_TPM_CRYPTO,
            auth_block.Create(user_input, &auth_state, &vkk_data)
                ->local_crypto_error());
}

// Test TpmEccAuthBlock::DeriveTest works correctly.
TEST(TpmEccAuthBlockTest, DeriveTest) {
  TpmEccAuthBlockState auth_block_state = GetDefaultEccAuthBlockState();

  brillo::Blob fake_hash(32, 'X');
  auth_block_state.tpm_public_key_hash =
      brillo::SecureBlob(fake_hash.begin(), fake_hash.end());

  // Set up the mock expectations.
  NiceMock<hwsec::MockCryptohomeFrontend> hwsec;
  SetupMockHwsec(hwsec);
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;
  EXPECT_CALL(hwsec, GetPubkeyHash(_)).WillOnce(ReturnValue(fake_hash));
  EXPECT_CALL(hwsec, PreloadSealedData(_)).WillOnce(Invoke([&](auto&&) {
    return hwsec::ScopedKey(hwsec::Key{.token = 5566},
                            hwsec.GetFakeMiddlewareDerivative());
  }));
  EXPECT_CALL(hwsec, GetAuthValue(_, _))
      .Times(Exactly(5))
      .WillRepeatedly(ReturnValue(brillo::SecureBlob()));

  brillo::SecureBlob fake_hvkkm(32, 'F');
  EXPECT_CALL(hwsec, UnsealWithCurrentUser(_, _, _))
      .WillOnce(ReturnValue(fake_hvkkm));

  TpmEccAuthBlock auth_block(&hwsec, &cryptohome_keys_manager);

  KeyBlobs key_out_data;
  std::optional<AuthBlock::SuggestedAction> suggested_action;
  AuthInput auth_input;
  auth_input.user_input = brillo::SecureBlob(20, 'E');
  auth_input.locked_to_single_user = false;

  AuthBlockState auth_state{.state = std::move(auth_block_state)};

  EXPECT_TRUE(
      auth_block
          .Derive(auth_input, auth_state, &key_out_data, &suggested_action)
          .ok());

  // Assert that the returned key blobs isn't uninitialized.
  EXPECT_NE(key_out_data.vkk_iv, std::nullopt);
  EXPECT_NE(key_out_data.vkk_key, std::nullopt);
  EXPECT_EQ(key_out_data.vkk_iv.value(), key_out_data.chaps_iv.value());
}

// Test TpmEccAuthBlock::Derive failure when there's no auth_input provided.
TEST(TpmEccAuthBlockTest, DeriveFailNoAuthInput) {
  TpmEccAuthBlockState auth_block_state = GetDefaultEccAuthBlockState();
  AuthBlockState auth_state{.state = std::move(auth_block_state)};

  NiceMock<hwsec::MockCryptohomeFrontend> hwsec;
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;
  TpmEccAuthBlock auth_block(&hwsec, &cryptohome_keys_manager);

  KeyBlobs key_out_data;
  std::optional<AuthBlock::SuggestedAction> suggested_action;
  AuthInput auth_input;
  EXPECT_EQ(
      auth_block
          .Derive(auth_input, auth_state, &key_out_data, &suggested_action)
          ->local_crypto_error(),
      CryptoError::CE_OTHER_CRYPTO);
}

// Test GetEccAuthValue in TpmEccAuthBlock::Derive failed as expected.
TEST(TpmEccAuthBlockTest, DeriveGetEccAuthFailTest) {
  TpmEccAuthBlockState auth_block_state = GetDefaultEccAuthBlockState();

  // Set up the mock expectations.
  NiceMock<hwsec::MockCryptohomeFrontend> hwsec;
  SetupMockHwsec(hwsec);
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;
  EXPECT_CALL(hwsec, PreloadSealedData(_)).WillOnce(ReturnValue(std::nullopt));

  EXPECT_CALL(hwsec, GetAuthValue(_, _))
      .WillOnce(ReturnError<TPMError>("fake", TPMRetryAction::kNoRetry));

  TpmEccAuthBlock auth_block(&hwsec, &cryptohome_keys_manager);

  KeyBlobs key_out_data;
  std::optional<AuthBlock::SuggestedAction> suggested_action;
  AuthInput auth_input;
  auth_input.user_input = brillo::SecureBlob(20, 'E');
  auth_input.locked_to_single_user = false;

  AuthBlockState auth_state{.state = std::move(auth_block_state)};

  EXPECT_EQ(
      CryptoError::CE_TPM_CRYPTO,
      auth_block
          .Derive(auth_input, auth_state, &key_out_data, &suggested_action)
          ->local_crypto_error());
}

// Test PreloadSealedData in TpmEccAuthBlock::Derive failed as expected.
TEST(TpmEccAuthBlockTest, DerivePreloadSealedDataFailTest) {
  TpmEccAuthBlockState auth_block_state = GetDefaultEccAuthBlockState();

  // Set up the mock expectations.
  NiceMock<hwsec::MockCryptohomeFrontend> hwsec;
  SetupMockHwsec(hwsec);
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;

  EXPECT_CALL(hwsec, PreloadSealedData(_))
      .WillOnce(ReturnError<TPMError>("fake", TPMRetryAction::kNoRetry));

  TpmEccAuthBlock auth_block(&hwsec, &cryptohome_keys_manager);

  KeyBlobs key_out_data;
  std::optional<AuthBlock::SuggestedAction> suggested_action;
  AuthInput auth_input;
  auth_input.user_input = brillo::SecureBlob(20, 'E');
  auth_input.locked_to_single_user = false;

  AuthBlockState auth_state{.state = std::move(auth_block_state)};

  EXPECT_EQ(
      CryptoError::CE_TPM_CRYPTO,
      auth_block
          .Derive(auth_input, auth_state, &key_out_data, &suggested_action)
          ->local_crypto_error());
}

// Test GetPublicKeyHash in TpmEccAuthBlock::Derive failed as expected.
TEST(TpmEccAuthBlockTest, DeriveGetPublicKeyHashFailTest) {
  TpmEccAuthBlockState auth_block_state = GetDefaultEccAuthBlockState();

  auth_block_state.tpm_public_key_hash = brillo::SecureBlob(32, 'X');

  // Set up the mock expectations.
  NiceMock<hwsec::MockCryptohomeFrontend> hwsec;
  SetupMockHwsec(hwsec);
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;
  EXPECT_CALL(hwsec, GetPubkeyHash(_))
      .WillOnce(ReturnError<TPMError>("fake", TPMRetryAction::kNoRetry));

  TpmEccAuthBlock auth_block(&hwsec, &cryptohome_keys_manager);

  KeyBlobs key_out_data;
  std::optional<AuthBlock::SuggestedAction> suggested_action;
  AuthInput auth_input;
  auth_input.user_input = brillo::SecureBlob(20, 'E');
  auth_input.locked_to_single_user = false;

  AuthBlockState auth_state{.state = std::move(auth_block_state)};

  EXPECT_EQ(
      CryptoError::CE_TPM_CRYPTO,
      auth_block
          .Derive(auth_input, auth_state, &key_out_data, &suggested_action)
          ->local_crypto_error());
}

// Test PublicKeyHashMismatch in TpmEccAuthBlock::Derive failed as expected.
TEST(TpmEccAuthBlockTest, DerivePublicKeyHashMismatchTest) {
  TpmEccAuthBlockState auth_block_state = GetDefaultEccAuthBlockState();

  auth_block_state.tpm_public_key_hash = brillo::SecureBlob(32, 'X');

  brillo::Blob fake_hash(32, 'Z');
  // Set up the mock expectations.
  NiceMock<hwsec::MockCryptohomeFrontend> hwsec;
  SetupMockHwsec(hwsec);
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;
  EXPECT_CALL(hwsec, GetPubkeyHash(_)).WillOnce(ReturnValue(fake_hash));

  TpmEccAuthBlock auth_block(&hwsec, &cryptohome_keys_manager);

  KeyBlobs key_out_data;
  std::optional<AuthBlock::SuggestedAction> suggested_action;
  AuthInput auth_input;
  auth_input.user_input = brillo::SecureBlob(20, 'E');
  auth_input.locked_to_single_user = false;

  AuthBlockState auth_state{.state = std::move(auth_block_state)};

  EXPECT_EQ(
      CryptoError::CE_TPM_FATAL,
      auth_block
          .Derive(auth_input, auth_state, &key_out_data, &suggested_action)
          ->local_crypto_error());
}

// Test the retry function in TpmEccAuthBlock::Derive failed as expected.
TEST(TpmEccAuthBlockTest, DeriveRetryFailTest) {
  TpmEccAuthBlockState auth_block_state = GetDefaultEccAuthBlockState();

  // Set up the mock expectations.
  NiceMock<hwsec::MockCryptohomeFrontend> hwsec;
  SetupMockHwsec(hwsec);
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;
  EXPECT_CALL(hwsec, PreloadSealedData(_)).WillOnce(ReturnValue(std::nullopt));

  // The TpmEccAuthBlock shouldn't retry forever if the TPM always returning
  // error.
  EXPECT_CALL(hwsec, GetAuthValue(_, _))
      .WillRepeatedly(ReturnError<TPMError>("reboot", TPMRetryAction::kReboot));

  TpmEccAuthBlock auth_block(&hwsec, &cryptohome_keys_manager);

  KeyBlobs key_out_data;
  std::optional<AuthBlock::SuggestedAction> suggested_action;
  AuthInput auth_input;
  auth_input.user_input = brillo::SecureBlob(20, 'E');
  auth_input.locked_to_single_user = true;

  AuthBlockState auth_state{.state = std::move(auth_block_state)};

  EXPECT_EQ(
      CryptoError::CE_TPM_REBOOT,
      auth_block
          .Derive(auth_input, auth_state, &key_out_data, &suggested_action)
          ->local_crypto_error());
}

// Test Unseal in TpmEccAuthBlock::Derive failed as expected.
TEST(TpmEccAuthBlockTest, DeriveUnsealFailTest) {
  TpmEccAuthBlockState auth_block_state = GetDefaultEccAuthBlockState();

  auth_block_state.tpm_public_key_hash = brillo::SecureBlob("public key hash");

  // Set up the mock expectations.
  NiceMock<hwsec::MockCryptohomeFrontend> hwsec;
  SetupMockHwsec(hwsec);
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;
  EXPECT_CALL(hwsec, PreloadSealedData(_)).WillOnce(ReturnValue(std::nullopt));
  EXPECT_CALL(hwsec, GetAuthValue(_, _))
      .Times(Exactly(5))
      .WillRepeatedly(ReturnValue(brillo::SecureBlob()));

  brillo::SecureBlob fake_hvkkm(32, 'F');
  EXPECT_CALL(hwsec, UnsealWithCurrentUser(_, _, _))
      .WillOnce(ReturnError<TPMError>("fake", TPMRetryAction::kNoRetry));

  TpmEccAuthBlock auth_block(&hwsec, &cryptohome_keys_manager);

  KeyBlobs key_out_data;
  std::optional<AuthBlock::SuggestedAction> suggested_action;
  AuthInput auth_input;
  auth_input.user_input = brillo::SecureBlob(20, 'E');
  auth_input.locked_to_single_user = false;

  AuthBlockState auth_state{.state = std::move(auth_block_state)};

  EXPECT_EQ(
      CryptoError::CE_TPM_CRYPTO,
      auth_block
          .Derive(auth_input, auth_state, &key_out_data, &suggested_action)
          ->local_crypto_error());
}

// Test CryptohomeKey in TpmEccAuthBlock::Derive failed as expected.
TEST(TpmEccAuthBlockTest, DeriveCryptohomeKeyFailTest) {
  TpmEccAuthBlockState auth_block_state = GetDefaultEccAuthBlockState();

  // Set up the mock expectations.
  NiceMock<hwsec::MockCryptohomeFrontend> hwsec;
  SetupMockHwsec(hwsec);
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager;

  EXPECT_CALL(*cryptohome_keys_manager.get_mock_cryptohome_key_loader(),
              HasCryptohomeKey())
      .WillRepeatedly(Return(false));

  TpmEccAuthBlock auth_block(&hwsec, &cryptohome_keys_manager);

  KeyBlobs key_out_data;
  std::optional<AuthBlock::SuggestedAction> suggested_action;
  AuthInput auth_input;
  auth_input.user_input = brillo::SecureBlob(20, 'E');
  auth_input.locked_to_single_user = true;

  AuthBlockState auth_state{.state = std::move(auth_block_state)};

  EXPECT_EQ(
      CryptoError::CE_TPM_REBOOT,
      auth_block
          .Derive(auth_input, auth_state, &key_out_data, &suggested_action)
          ->local_crypto_error());
}

}  // namespace cryptohome
