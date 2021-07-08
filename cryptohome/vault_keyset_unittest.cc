// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Unit tests for VaultKeyset.

#include "cryptohome/vault_keyset.h"

#include <memory>
#include <string.h>  // For memcmp().
#include <vector>
#include <openssl/evp.h>

#include <base/files/file_path.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <brillo/secure_blob.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "cryptohome/crypto.h"
#include "cryptohome/crypto/aes.h"
#include "cryptohome/crypto/hmac.h"
#include "cryptohome/crypto/secure_blob_util.h"
#include "cryptohome/crypto_error.h"
#include "cryptohome/cryptohome_common.h"
#include "cryptohome/mock_cryptohome_keys_manager.h"
#include "cryptohome/mock_le_credential_manager.h"
#include "cryptohome/mock_platform.h"
#include "cryptohome/mock_tpm.h"

namespace cryptohome {
using base::FilePath;
using brillo::SecureBlob;

using ::testing::_;
using ::testing::DoAll;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::SaveArg;
using ::testing::SetArgPointee;
using ::testing::WithArg;

class VaultKeysetTest : public ::testing::Test {
 public:
  VaultKeysetTest() {}
  VaultKeysetTest(const VaultKeysetTest&) = delete;
  VaultKeysetTest& operator=(const VaultKeysetTest&) = delete;

  virtual ~VaultKeysetTest() {}

  static bool FindBlobInBlob(const brillo::SecureBlob& haystack,
                             const brillo::SecureBlob& needle) {
    if (needle.size() > haystack.size()) {
      return false;
    }
    for (unsigned int start = 0; start <= (haystack.size() - needle.size());
         start++) {
      if (memcmp(&haystack[start], needle.data(), needle.size()) == 0) {
        return true;
      }
    }
    return false;
  }

 protected:
  MockPlatform platform_;
};

TEST_F(VaultKeysetTest, AllocateRandom) {
  // Check that allocating a random VaultKeyset works
  Crypto crypto(&platform_);
  VaultKeyset vault_keyset;
  vault_keyset.Initialize(&platform_, &crypto);
  vault_keyset.CreateRandom();

  EXPECT_EQ(CRYPTOHOME_DEFAULT_KEY_SIZE, vault_keyset.GetFek().size());
  EXPECT_EQ(CRYPTOHOME_DEFAULT_KEY_SIGNATURE_SIZE,
            vault_keyset.GetFekSig().size());
  EXPECT_EQ(CRYPTOHOME_DEFAULT_KEY_SALT_SIZE, vault_keyset.GetFekSalt().size());

  EXPECT_EQ(CRYPTOHOME_DEFAULT_KEY_SIZE, vault_keyset.GetFnek().size());
  EXPECT_EQ(CRYPTOHOME_DEFAULT_KEY_SIGNATURE_SIZE,
            vault_keyset.GetFnekSig().size());
  EXPECT_EQ(CRYPTOHOME_DEFAULT_KEY_SALT_SIZE,
            vault_keyset.GetFnekSalt().size());
  EXPECT_EQ(CRYPTOHOME_CHAPS_KEY_LENGTH, vault_keyset.GetChapsKey().size());
}

TEST_F(VaultKeysetTest, SerializeTest) {
  // Check that serialize works
  Crypto crypto(&platform_);
  VaultKeyset vault_keyset;
  vault_keyset.Initialize(&platform_, &crypto);
  vault_keyset.CreateRandom();

  SecureBlob blob;
  EXPECT_TRUE(vault_keyset.ToKeysBlob(&blob));

  EXPECT_TRUE(VaultKeysetTest::FindBlobInBlob(blob, vault_keyset.GetFek()));
  EXPECT_TRUE(VaultKeysetTest::FindBlobInBlob(blob, vault_keyset.GetFekSig()));
  EXPECT_TRUE(VaultKeysetTest::FindBlobInBlob(blob, vault_keyset.GetFekSalt()));

  EXPECT_TRUE(VaultKeysetTest::FindBlobInBlob(blob, vault_keyset.GetFnek()));
  EXPECT_TRUE(VaultKeysetTest::FindBlobInBlob(blob, vault_keyset.GetFnekSig()));
  EXPECT_TRUE(
      VaultKeysetTest::FindBlobInBlob(blob, vault_keyset.GetFnekSalt()));
}

TEST_F(VaultKeysetTest, DeserializeTest) {
  // Check that deserialize works
  Crypto crypto(&platform_);
  VaultKeyset vault_keyset;
  vault_keyset.Initialize(&platform_, &crypto);
  vault_keyset.CreateRandom();

  SecureBlob blob;
  EXPECT_TRUE(vault_keyset.ToKeysBlob(&blob));

  VaultKeyset new_vault_keyset;
  new_vault_keyset.FromKeysBlob(blob);

  EXPECT_EQ(vault_keyset.GetFek().size(), new_vault_keyset.GetFek().size());
  EXPECT_TRUE(VaultKeysetTest::FindBlobInBlob(vault_keyset.GetFek(),
                                              new_vault_keyset.GetFek()));
  EXPECT_EQ(vault_keyset.GetFekSig().size(),
            new_vault_keyset.GetFekSig().size());
  EXPECT_TRUE(VaultKeysetTest::FindBlobInBlob(vault_keyset.GetFekSig(),
                                              new_vault_keyset.GetFekSig()));
  EXPECT_EQ(vault_keyset.GetFekSalt().size(),
            new_vault_keyset.GetFekSalt().size());
  EXPECT_TRUE(VaultKeysetTest::FindBlobInBlob(vault_keyset.GetFekSalt(),
                                              new_vault_keyset.GetFekSalt()));

  EXPECT_EQ(vault_keyset.GetFnek().size(), new_vault_keyset.GetFnek().size());
  EXPECT_TRUE(VaultKeysetTest::FindBlobInBlob(vault_keyset.GetFnek(),
                                              new_vault_keyset.GetFnek()));
  EXPECT_EQ(vault_keyset.GetFnekSig().size(),
            new_vault_keyset.GetFnekSig().size());
  EXPECT_TRUE(VaultKeysetTest::FindBlobInBlob(vault_keyset.GetFnekSig(),
                                              new_vault_keyset.GetFnekSig()));
  EXPECT_EQ(vault_keyset.GetFnekSalt().size(),
            new_vault_keyset.GetFnekSalt().size());
  EXPECT_TRUE(VaultKeysetTest::FindBlobInBlob(vault_keyset.GetFnekSalt(),
                                              new_vault_keyset.GetFnekSalt()));
}

ACTION_P(CopyToSecureBlob, b) {
  b->assign(arg0.begin(), arg0.end());
  return true;
}

ACTION_P(CopyFromSecureBlob, b) {
  arg0->assign(b->begin(), b->end());
  return true;
}

TEST_F(VaultKeysetTest, LoadSaveTest) {
  MockPlatform platform;
  Crypto crypto(&platform);
  VaultKeyset keyset;
  keyset.Initialize(&platform, &crypto);

  keyset.CreateRandom();
  SecureBlob bytes;

  static const int kTestTimestamp = 123;
  static const int kFscryptPolicyVersion = 2;
  cryptohome::Timestamp timestamp;
  timestamp.set_timestamp(kTestTimestamp);
  SecureBlob tbytes(timestamp.ByteSizeLong());
  google::protobuf::uint8* buf =
      static_cast<google::protobuf::uint8*>(tbytes.data());
  timestamp.SerializeWithCachedSizesToArray(buf);

  keyset.SetFSCryptPolicyVersion(kFscryptPolicyVersion);

  EXPECT_CALL(platform, WriteFileAtomicDurable(FilePath("foo"), _, _))
      .WillOnce(WithArg<1>(CopyToSecureBlob(&bytes)));
  EXPECT_CALL(platform, ReadFile(FilePath("foo"), _))
      .WillOnce(WithArg<1>(CopyFromSecureBlob(&bytes)));

  EXPECT_CALL(platform, ReadFile(FilePath("foo").AddExtension("timestamp"), _))
      .WillOnce(WithArg<1>(CopyFromSecureBlob(&tbytes)));

  SecureBlob key("key");
  EXPECT_TRUE(keyset.Encrypt(key, ""));
  EXPECT_TRUE(keyset.Save(FilePath("foo")));

  VaultKeyset new_keyset;
  new_keyset.Initialize(&platform, &crypto);
  EXPECT_TRUE(new_keyset.Load(FilePath("foo")));
  ASSERT_TRUE(new_keyset.HasLastActivityTimestamp());
  EXPECT_EQ(kTestTimestamp, new_keyset.GetLastActivityTimestamp());
  EXPECT_TRUE(new_keyset.Decrypt(key, false /* locked_to_single_user */,
                                 nullptr /* crypto_error */));
  EXPECT_EQ(new_keyset.GetFSCryptPolicyVersion(), kFscryptPolicyVersion);
}

TEST_F(VaultKeysetTest, WriteError) {
  MockPlatform platform;
  Crypto crypto(&platform);
  VaultKeyset keyset;
  keyset.Initialize(&platform, &crypto);

  keyset.CreateRandom();
  SecureBlob bytes;

  EXPECT_CALL(platform, WriteFileAtomicDurable(FilePath("foo"), _, _))
      .WillOnce(Return(false));

  SecureBlob key("key");
  EXPECT_TRUE(keyset.Encrypt(key, ""));
  EXPECT_FALSE(keyset.Save(FilePath("foo")));
}

TEST_F(VaultKeysetTest, AuthLockedDefault) {
  MockPlatform platform;
  Crypto crypto(&platform);
  VaultKeyset keyset;
  keyset.Initialize(&platform, &crypto);

  static const int kFscryptPolicyVersion = 2;

  keyset.CreateRandom();
  keyset.SetFSCryptPolicyVersion(kFscryptPolicyVersion);
  keyset.SetFlags(SerializedVaultKeyset::LE_CREDENTIAL);

  SecureBlob key("key");
  EXPECT_TRUE(keyset.Encrypt(key, ""));
  EXPECT_FALSE(keyset.GetAuthLocked());
}

TEST_F(VaultKeysetTest, GetPcrBoundAuthBlockStateTest) {
  MockPlatform platform;
  Crypto crypto(&platform);
  VaultKeyset keyset;
  keyset.Initialize(&platform, &crypto);

  keyset.CreateRandom();
  keyset.SetFlags(SerializedVaultKeyset::TPM_WRAPPED |
                  SerializedVaultKeyset::SCRYPT_DERIVED |
                  SerializedVaultKeyset::PCR_BOUND);
  keyset.SetTpmPublicKeyHash(brillo::SecureBlob("yadayada"));
  keyset.SetTPMKey(brillo::SecureBlob("blabla"));
  keyset.SetExtendedTPMKey(brillo::SecureBlob("foobaz"));

  AuthBlockState auth_state;
  EXPECT_TRUE(keyset.GetAuthBlockState(&auth_state));

  EXPECT_TRUE(auth_state.has_tpm_bound_to_pcr_state());
  EXPECT_TRUE(auth_state.tpm_bound_to_pcr_state().scrypt_derived());
  EXPECT_TRUE(auth_state.tpm_bound_to_pcr_state().has_extended_tpm_key());
  EXPECT_TRUE(auth_state.tpm_bound_to_pcr_state().has_tpm_key());
}

TEST_F(VaultKeysetTest, GetNotPcrBoundAuthBlockState) {
  MockPlatform platform;
  Crypto crypto(&platform);
  VaultKeyset keyset;
  keyset.Initialize(&platform, &crypto);

  keyset.CreateRandom();
  keyset.SetFlags(SerializedVaultKeyset::TPM_WRAPPED);
  keyset.SetTpmPublicKeyHash(brillo::SecureBlob("yadayada"));
  keyset.SetTPMKey(brillo::SecureBlob("blabla"));

  AuthBlockState auth_state;
  EXPECT_TRUE(keyset.GetAuthBlockState(&auth_state));

  EXPECT_TRUE(auth_state.has_tpm_not_bound_to_pcr_state());
  EXPECT_FALSE(auth_state.tpm_not_bound_to_pcr_state().scrypt_derived());
  EXPECT_TRUE(auth_state.tpm_not_bound_to_pcr_state().has_tpm_key());
}

TEST_F(VaultKeysetTest, GetPinWeaverAuthBlockState) {
  MockPlatform platform;
  Crypto crypto(&platform);
  VaultKeyset keyset;
  keyset.Initialize(&platform, &crypto);

  const uint64_t le_label = 012345;
  keyset.CreateRandom();
  keyset.SetFlags(SerializedVaultKeyset::LE_CREDENTIAL);
  keyset.SetLELabel(le_label);

  AuthBlockState auth_state;
  EXPECT_TRUE(keyset.GetAuthBlockState(&auth_state));

  EXPECT_TRUE(auth_state.has_pin_weaver_state());
  EXPECT_TRUE(auth_state.pin_weaver_state().has_le_label());
  EXPECT_EQ(le_label, auth_state.pin_weaver_state().le_label());
}

TEST_F(VaultKeysetTest, GetChallengeCredentialAuthBlockState) {
  MockPlatform platform;
  Crypto crypto(&platform);
  VaultKeyset keyset;
  keyset.Initialize(&platform, &crypto);

  keyset.CreateRandom();
  keyset.SetFlags(SerializedVaultKeyset::SCRYPT_WRAPPED |
                  SerializedVaultKeyset::SIGNATURE_CHALLENGE_PROTECTED);

  AuthBlockState auth_state;
  EXPECT_TRUE(keyset.GetAuthBlockState(&auth_state));

  EXPECT_TRUE(auth_state.has_challenge_credential_state());
}

TEST_F(VaultKeysetTest, GetLibscryptCompatAuthBlockState) {
  MockPlatform platform;
  Crypto crypto(&platform);
  VaultKeyset keyset;
  keyset.Initialize(&platform, &crypto);

  keyset.CreateRandom();
  keyset.SetFlags(SerializedVaultKeyset::SCRYPT_WRAPPED);
  keyset.SetWrappedKeyset(brillo::SecureBlob("foo"));
  keyset.SetWrappedChapsKey(brillo::SecureBlob("bar"));
  keyset.SetWrappedResetSeed(brillo::SecureBlob("baz"));

  AuthBlockState auth_state;
  EXPECT_TRUE(keyset.GetAuthBlockState(&auth_state));

  EXPECT_TRUE(auth_state.has_libscrypt_compat_state());
  EXPECT_TRUE(auth_state.libscrypt_compat_state().has_wrapped_keyset());
  EXPECT_TRUE(auth_state.libscrypt_compat_state().has_wrapped_chaps_key());
  EXPECT_TRUE(auth_state.libscrypt_compat_state().has_wrapped_reset_seed());
}

TEST_F(VaultKeysetTest, GetDoubleWrappedCompatAuthBlockState) {
  MockPlatform platform;
  Crypto crypto(&platform);
  VaultKeyset keyset;
  keyset.Initialize(&platform, &crypto);

  keyset.CreateRandom();
  keyset.SetFlags(SerializedVaultKeyset::SCRYPT_WRAPPED |
                  SerializedVaultKeyset::TPM_WRAPPED);

  AuthBlockState auth_state;
  keyset.GetAuthBlockState(&auth_state);

  EXPECT_TRUE(auth_state.has_double_wrapped_compat_state());
}

TEST_F(VaultKeysetTest, EncryptionTest) {
  // Check that EncryptVaultKeyset returns something other than the bytes passed
  Crypto crypto(&platform_);

  VaultKeyset vault_keyset;
  vault_keyset.Initialize(&platform_, &crypto);
  vault_keyset.CreateRandom();

  SecureBlob key(20);
  GetSecureRandom(key.data(), key.size());
  SecureBlob salt(PKCS5_SALT_LEN);
  GetSecureRandom(salt.data(), salt.size());

  AuthBlockState auth_block_state;
  ASSERT_TRUE(
      vault_keyset.EncryptVaultKeyset(key, salt, "", &auth_block_state));
}

TEST_F(VaultKeysetTest, DecryptionTest) {
  // Check that DecryptVaultKeyset returns the original keyset
  MockPlatform platform;
  Crypto crypto(&platform);

  VaultKeyset vault_keyset;
  vault_keyset.Initialize(&platform_, &crypto);
  vault_keyset.CreateRandom();

  SecureBlob key(20);
  GetSecureRandom(key.data(), key.size());
  SecureBlob salt(PKCS5_SALT_LEN);
  GetSecureRandom(salt.data(), salt.size());
  vault_keyset.salt_ = salt;

  AuthBlockState auth_block_state;
  ASSERT_TRUE(
      vault_keyset.EncryptVaultKeyset(key, salt, "", &auth_block_state));

  // TODO(kerrnel): This is a hack to bridge things until DecryptVaultKeyset is
  // modified to take a key material and an auth block state.
  vault_keyset.SetAuthBlockState(auth_block_state);

  SecureBlob original_data;
  ASSERT_TRUE(vault_keyset.ToKeysBlob(&original_data));

  CryptoError crypto_error = CryptoError::CE_NONE;
  ASSERT_TRUE(vault_keyset.DecryptVaultKeyset(
      key, false /* locked_to_single_user */, &crypto_error));

  SecureBlob new_data;
  ASSERT_TRUE(vault_keyset.ToKeysBlob(&new_data));

  EXPECT_EQ(new_data.size(), original_data.size());
  ASSERT_TRUE(VaultKeysetTest::FindBlobInBlob(new_data, original_data));
}

namespace {
constexpr char kHexHeSecret[] =
    "F3D9D5B126C36676689E18BB8517D95DF4F30947E71D4A840824425760B1D3FA";
constexpr char kHexResetSecret[] =
    "B133D2450392335BA8D33AA95AD52488254070C66F5D79AEA1A46AC4A30760D4";
constexpr char kHexWrappedKeyset[] =
    "B737B5D73E39BD390A4F361CE2FC166CF1E89EC6AEAA35D4B34456502C48B4F5EFA310077"
    "324B393E13AF633DF3072FF2EC78BD2B80D919035DB97C30F1AD418737DA3F26A4D35DF6B"
    "6A9743BD0DF3D37D8A68DE0932A9905452D05ECF92701B9805937F76EE01D10924268F057"
    "EDD66087774BB86C2CB92B01BD3A3C41C10C52838BD3A3296474598418E5191DEE9E8D831"
    "3C859C9EDB0D5F2BC1D7FC3C108A0D4ABB2D90E413086BCFFD0902AB68E2BF787817EB10C"
    "25E2E43011CAB3FB8AA";
constexpr char kHexSalt[] = "D470B9B108902241";
constexpr char kHexVaultKey[] =
    "665A58534E684F2B61516B6D42624B514E6749732B4348427450305453754158377232347"
    "37A79466C6B383D";
constexpr char kHexFekIv[] = "EA80F14BF29C6D580D536E7F0CC47F3E";
constexpr char kHexChapsIv[] = "ED85D928940E5B02ED218F29225AA34F";
constexpr char kHexWrappedChapsKey[] =
    "7D7D01EECC8DAE7906CAD56310954BBEB3CC81765210D29902AB92DDE074217771AD284F2"
    "12C13897C6CBB30CEC4CD75";

std::string HexDecode(const std::string& hex) {
  std::vector<uint8_t> output;
  CHECK(base::HexStringToBytes(hex, &output));
  return std::string(output.begin(), output.end());
}
}  // namespace

class LeCredentialsManagerTest : public ::testing::Test {
 public:
  LeCredentialsManagerTest() : crypto_(&platform_) {
    EXPECT_CALL(cryptohome_keys_manager_, Init())
        .WillOnce(Return());  // because HasCryptohomeKey returned false once.

    EXPECT_CALL(tpm_, IsEnabled()).WillRepeatedly(Return(true));
    EXPECT_CALL(tpm_, IsOwned()).WillRepeatedly(Return(true));

    // Raw pointer as crypto expects unique_ptr, which we will wrap this
    // allocation into.
    le_cred_manager_ = new MockLECredentialManager();
    EXPECT_CALL(*le_cred_manager_, CheckCredential(_, _, _, _))
        .WillRepeatedly(DoAll(
            SetArgPointee<2>(brillo::SecureBlob(HexDecode(kHexHeSecret))),
            SetArgPointee<3>(brillo::SecureBlob(HexDecode(kHexResetSecret))),
            Return(LE_CRED_SUCCESS)));
    crypto_.set_le_manager_for_testing(
        std::unique_ptr<cryptohome::LECredentialManager>(le_cred_manager_));

    crypto_.Init(&tpm_, &cryptohome_keys_manager_);

    pin_vault_keyset_.Initialize(&platform_, &crypto_);
  }

  ~LeCredentialsManagerTest() override = default;

  // Not copyable or movable
  LeCredentialsManagerTest(const LeCredentialsManagerTest&) = delete;
  LeCredentialsManagerTest& operator=(const LeCredentialsManagerTest&) = delete;
  LeCredentialsManagerTest(LeCredentialsManagerTest&&) = delete;
  LeCredentialsManagerTest& operator=(LeCredentialsManagerTest&&) = delete;

 protected:
  MockPlatform platform_;
  Crypto crypto_;
  NiceMock<MockTpm> tpm_;
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager_;
  MockLECredentialManager* le_cred_manager_;

  VaultKeyset pin_vault_keyset_;
};

TEST_F(LeCredentialsManagerTest, Encrypt) {
  EXPECT_CALL(*le_cred_manager_, InsertCredential(_, _, _, _, _, _))
      .WillOnce(Return(LE_CRED_SUCCESS));

  pin_vault_keyset_.CreateRandom();
  pin_vault_keyset_.SetLowEntropyCredential(true);

  // This used to happen in VaultKeyset::EncryptVaultKeyset, but now happens in
  // VaultKeyset::Encrypt and thus needs to be done manually here.
  pin_vault_keyset_.reset_seed_ = CreateSecureRandomBlob(kAesBlockSize);
  pin_vault_keyset_.reset_salt_ = CreateSecureRandomBlob(kAesBlockSize);
  pin_vault_keyset_.reset_secret_ = HmacSha256(
      pin_vault_keyset_.reset_salt_.value(), pin_vault_keyset_.reset_seed_);

  AuthBlockState auth_block_state;
  EXPECT_TRUE(pin_vault_keyset_.EncryptVaultKeyset(
      brillo::SecureBlob(HexDecode(kHexVaultKey)),
      brillo::SecureBlob(HexDecode(kHexSalt)), "unused", &auth_block_state));

  EXPECT_TRUE(auth_block_state.has_pin_weaver_state());
}

TEST_F(LeCredentialsManagerTest, EncryptFail) {
  EXPECT_CALL(*le_cred_manager_, InsertCredential(_, _, _, _, _, _))
      .WillOnce(Return(LE_CRED_ERROR_NO_FREE_LABEL));

  pin_vault_keyset_.CreateRandom();
  pin_vault_keyset_.SetLowEntropyCredential(true);

  // This used to happen in VaultKeyset::EncryptVaultKeyset, but now happens in
  // VaultKeyset::Encrypt and thus needs to be done manually here.
  pin_vault_keyset_.reset_seed_ = CreateSecureRandomBlob(kAesBlockSize);
  pin_vault_keyset_.reset_salt_ = CreateSecureRandomBlob(kAesBlockSize);
  pin_vault_keyset_.reset_secret_ = HmacSha256(
      pin_vault_keyset_.reset_salt_.value(), pin_vault_keyset_.reset_seed_);

  AuthBlockState auth_block_state;
  EXPECT_FALSE(pin_vault_keyset_.EncryptVaultKeyset(
      brillo::SecureBlob(HexDecode(kHexVaultKey)),
      brillo::SecureBlob(HexDecode(kHexSalt)), "unused", &auth_block_state));
}

TEST_F(LeCredentialsManagerTest, Decrypt) {
  VaultKeyset vk;
  // vk needs its Crypto object set to be able to create the AuthBlock in the
  // DecryptVaultKeyset() call.
  vk.Initialize(&platform_, &crypto_);

  SerializedVaultKeyset serialized;
  serialized.set_flags(SerializedVaultKeyset::LE_CREDENTIAL);
  serialized.set_le_fek_iv(HexDecode(kHexFekIv));
  serialized.set_le_chaps_iv(HexDecode(kHexChapsIv));
  serialized.set_wrapped_keyset(HexDecode(kHexWrappedKeyset));
  serialized.set_wrapped_chaps_key(HexDecode(kHexWrappedChapsKey));
  serialized.set_salt(HexDecode(kHexSalt));
  serialized.set_le_label(0644);

  vk.InitializeFromSerialized(serialized);
  AuthBlockState auth_state;
  EXPECT_TRUE(vk.GetAuthBlockState(&auth_state));

  CryptoError crypto_error = CryptoError::CE_NONE;
  EXPECT_TRUE(vk.DecryptVaultKeyset(brillo::SecureBlob(HexDecode(kHexVaultKey)),
                                    false, &crypto_error));
  EXPECT_EQ(CryptoError::CE_NONE, crypto_error);
}

// crbug.com/1224150: auth_locked must be set to false when an LE credential is
// re-saved.
TEST_F(LeCredentialsManagerTest, EncryptTestReset) {
  EXPECT_CALL(*le_cred_manager_, InsertCredential(_, _, _, _, _, _))
      .WillOnce(Return(LE_CRED_SUCCESS));

  pin_vault_keyset_.CreateRandom();
  pin_vault_keyset_.SetLowEntropyCredential(true);

  // This used to happen in VaultKeyset::EncryptVaultKeyset, but now happens in
  // VaultKeyset::Encrypt and thus needs to be done manually here.
  pin_vault_keyset_.reset_seed_ = CreateSecureRandomBlob(kAesBlockSize);
  pin_vault_keyset_.reset_salt_ = CreateSecureRandomBlob(kAesBlockSize);
  pin_vault_keyset_.reset_secret_ = HmacSha256(
      pin_vault_keyset_.reset_salt_.value(), pin_vault_keyset_.reset_seed_);
  pin_vault_keyset_.auth_locked_ = true;

  SecureBlob key("key");
  EXPECT_TRUE(pin_vault_keyset_.Encrypt(key, "foo@gmail.com"));
  EXPECT_TRUE(pin_vault_keyset_.HasKeyData());
  EXPECT_FALSE(pin_vault_keyset_.auth_locked_);

  const SerializedVaultKeyset& serialized = pin_vault_keyset_.ToSerialized();
  EXPECT_FALSE(serialized.key_data().policy().auth_locked());
}

}  // namespace cryptohome
