// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Unit tests for VaultKeyset.

#include "cryptohome/vault_keyset.h"

#include <memory>
#include <string.h>  // For memcmp().
#include <utility>
#include <variant>
#include <vector>
#include <openssl/evp.h>

#include <base/files/file_path.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <brillo/secure_blob.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "cryptohome/auth_blocks/auth_block.h"
#include "cryptohome/auth_blocks/auth_block_state.h"
#include "cryptohome/auth_blocks/auth_block_utils.h"
#include "cryptohome/auth_blocks/libscrypt_compat_auth_block.h"
#include "cryptohome/auth_blocks/pin_weaver_auth_block.h"
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
#include "cryptohome/storage/file_system_keyset.h"

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

namespace {
constexpr char kHexHighEntropySecret[] =
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

constexpr int kLegacyIndex = 1;
constexpr char kLegacyLabel[] = "legacy-1";
constexpr char kTempLabel[] = "tempLabel";

constexpr char kFilePath[] = "foo";
constexpr char kPasswordKey[] = "key";
constexpr char kObfuscatedUsername[] = "foo@gmail.com";
constexpr char kFakePasswordKey[] = "blabla";

constexpr int kPasswordRounds = 5;

// Generated with this command:
// cryptohome --action=mount_ex --user=fakeuser1@example.com
// --key_label=PasswordLabel --create --password=FakePasswordForFakeUser1
constexpr char kHexLibScryptExampleSerializedVaultKeyset[] =
    "0802120869528fca742022fd1ab402736372797074000e00000008000000"
    "019602e181d3047f2560d889b199015da9a2786101a1d491dccc7a9bd516"
    "2d4ef72cd09817ab78dd27355bd45f5dd2c66a89f9b4c7911d2a85126e2a"
    "ee5df1a88dceaa1b4adb5b98fc0107f5bafd881fb8b552cef71067cdfa39"
    "6d11c51e5467a8937c393687eb407de488015ec793fe87bf5cd6987ff50d"
    "e13111ee4604b774b951adc18ccc3ae0132e842df56b38e8256238fa3205"
    "8ae9425451c54f8527669ad67888b64deabdf974d701ff7c429942979edf"
    "ae07b8cf6b82e6a11c764ab216814de8652706c6aedc66f3ec7da371fd92"
    "912742879e8bae064970b453c9e94d5f3677b80103f958599f8ee9aa6e68"
    "3d497e4cc464666b71ec25c67336713cfb79020ee36a0ef2ae8a210c0b97"
    "9e0ec287d0b622f7706ea7ace69c856ecc37b2221e5fb34a13120d506173"
    "73776f72644c6162656c42021000529001736372797074000e0000000800"
    "000001b9eed4ad3694dc8fcec59a06c16e508829e99bf1a45dabb1298574"
    "c873f885d9355b3294bd243e40382fda5651ae094ab270188068d42e3bd8"
    "320bbb57a165a613d70998310e9c6c3ea1f6759603275d22968ca3bda165"
    "dc5dbc77921411ae5ba426ea84fcb29e8ee7c758be9a2e1c544d2834bd2c"
    "ea69f49b484e68fca167265aa001736372797074000e0000000800000001"
    "6f632b3a3faab2347327f58e4146fc00b1dddea4e7971caf7b3a49b6c02e"
    "8ad24fb05076c16b7d1065df6379ef34b54a97231edb7393a7446beec328"
    "afc962c24e123dd9e81a451c4f0f670f20e51662171c319127f96fd2718d"
    "d6e73b29f32b86ffcc3cf115243f810ddcdc9be1e2ba3aba5d30cf3457e8"
    "02f9da1d6c5934af7651cd9cca3d53ab5c6cafc057f52e8b";

constexpr char kHexLibScryptExampleSalt[] = "69528fca742022fd";
constexpr char kHexLibScryptExamplePasskey[] =
    "6335336231666336333130336466313430356266626235336630303133366264";
constexpr char kHexLibScryptExampleFek[] =
    "1b70e790b9d48ae2d695bfba06ee8b47bece82c990569e191a79b9c1a256fa7140f1e69090"
    "eb2c59d4370a9ff9bc623989c72b3617013a91c8ad52ab9c80d8a1";
constexpr char kHexLibScryptExampleFekSig[] = "7535f385362a8450";
constexpr char kHexLibScryptExampleFekSalt[] = "4e8f98e96de8d6ae";
constexpr char kHexLibScryptExampleFnek[] =
    "0ccf1c6a7e319665f843f950de0b9f82ce72ddb2e8eb4727a7c7b4786fbf307dc861696f36"
    "a17044bd4f69949269088fab95cea159354a4968252d510c1e93a1";
constexpr char kHexLibScryptExampleFnekSig[] = "71cb91c3ab4f2721";
constexpr char kHexLibScryptExampleFnekSalt[] = "65ee2c9d0fea7161";

std::string HexDecode(const std::string& hex) {
  std::vector<uint8_t> output;
  CHECK(base::HexStringToBytes(hex, &output));
  return std::string(output.begin(), output.end());
}
}  // namespace

class VaultKeysetTest : public ::testing::Test {
 public:
  VaultKeysetTest() : crypto_(&platform_) {}
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
  Crypto crypto_;
  NiceMock<MockTpm> tpm_;
};

TEST_F(VaultKeysetTest, AllocateRandom) {
  // Check that allocating a random VaultKeyset works
  VaultKeyset vault_keyset;
  vault_keyset.Initialize(&platform_, &crypto_);
  vault_keyset.CreateFromFileSystemKeyset(FileSystemKeyset::CreateRandom());

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
  VaultKeyset vault_keyset;
  vault_keyset.Initialize(&platform_, &crypto_);
  vault_keyset.CreateFromFileSystemKeyset(FileSystemKeyset::CreateRandom());

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
  VaultKeyset vault_keyset;
  vault_keyset.Initialize(&platform_, &crypto_);
  vault_keyset.CreateFromFileSystemKeyset(FileSystemKeyset::CreateRandom());

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
  VaultKeyset keyset;
  keyset.Initialize(&platform_, &crypto_);

  keyset.CreateFromFileSystemKeyset(FileSystemKeyset::CreateRandom());
  SecureBlob bytes;

  static const int kFscryptPolicyVersion = 2;

  keyset.SetFSCryptPolicyVersion(kFscryptPolicyVersion);

  EXPECT_CALL(platform_, WriteFileAtomicDurable(FilePath(kFilePath), _, _))
      .WillOnce(WithArg<1>(CopyToSecureBlob(&bytes)));
  EXPECT_CALL(platform_, ReadFile(FilePath(kFilePath), _))
      .WillOnce(WithArg<1>(CopyFromSecureBlob(&bytes)));

  SecureBlob key(kPasswordKey);
  std::string obfuscated_username(kObfuscatedUsername);
  EXPECT_TRUE(keyset.Encrypt(key, obfuscated_username));
  EXPECT_TRUE(keyset.Save(FilePath(kFilePath)));

  VaultKeyset new_keyset;
  new_keyset.Initialize(&platform_, &crypto_);
  EXPECT_TRUE(new_keyset.Load(FilePath(kFilePath)));
  EXPECT_TRUE(new_keyset.Decrypt(key, false /* locked_to_single_user */,
                                 nullptr /* crypto_error */));
  EXPECT_EQ(new_keyset.GetFSCryptPolicyVersion(), kFscryptPolicyVersion);
}

TEST_F(VaultKeysetTest, WriteError) {
  VaultKeyset keyset;
  keyset.Initialize(&platform_, &crypto_);

  keyset.CreateFromFileSystemKeyset(FileSystemKeyset::CreateRandom());
  SecureBlob bytes;

  EXPECT_CALL(platform_, WriteFileAtomicDurable(FilePath(kFilePath), _, _))
      .WillOnce(Return(false));

  SecureBlob key(kPasswordKey);
  std::string obfuscated_username(kObfuscatedUsername);
  EXPECT_TRUE(keyset.Encrypt(key, obfuscated_username));
  EXPECT_FALSE(keyset.Save(FilePath(kFilePath)));
}

TEST_F(VaultKeysetTest, AuthLockedDefault) {
  VaultKeyset keyset;
  keyset.Initialize(&platform_, &crypto_);

  static const int kFscryptPolicyVersion = 2;

  keyset.CreateFromFileSystemKeyset(FileSystemKeyset::CreateRandom());
  keyset.SetFSCryptPolicyVersion(kFscryptPolicyVersion);
  keyset.SetFlags(SerializedVaultKeyset::LE_CREDENTIAL);

  SecureBlob key(kPasswordKey);
  std::string obfuscated_username(kObfuscatedUsername);
  EXPECT_TRUE(keyset.Encrypt(key, obfuscated_username));
  EXPECT_FALSE(keyset.GetAuthLocked());
}

TEST_F(VaultKeysetTest, GetPcrBoundAuthBlockStateTest) {
  VaultKeyset keyset;
  keyset.Initialize(&platform_, &crypto_);

  keyset.CreateFromFileSystemKeyset(FileSystemKeyset::CreateRandom());
  keyset.SetFlags(SerializedVaultKeyset::TPM_WRAPPED |
                  SerializedVaultKeyset::SCRYPT_DERIVED |
                  SerializedVaultKeyset::PCR_BOUND);
  keyset.SetTpmPublicKeyHash(brillo::SecureBlob("yadayada"));
  keyset.SetTPMKey(brillo::SecureBlob("blabla"));
  keyset.SetExtendedTPMKey(brillo::SecureBlob("foobaz"));

  AuthBlockState auth_state;
  EXPECT_TRUE(GetAuthBlockState(keyset, auth_state));

  const TpmBoundToPcrAuthBlockState* tpm_state =
      std::get_if<TpmBoundToPcrAuthBlockState>(&auth_state.state);

  ASSERT_NE(tpm_state, nullptr);
  ASSERT_TRUE(tpm_state->scrypt_derived.has_value());
  EXPECT_TRUE(tpm_state->scrypt_derived.value());
  EXPECT_TRUE(tpm_state->extended_tpm_key.has_value());
  EXPECT_TRUE(tpm_state->tpm_key.has_value());
}

TEST_F(VaultKeysetTest, GetEccAuthBlockStateTest) {
  VaultKeyset keyset;
  keyset.Initialize(&platform_, &crypto_);

  keyset.CreateFromFileSystemKeyset(FileSystemKeyset::CreateRandom());
  keyset.SetFlags(SerializedVaultKeyset::TPM_WRAPPED |
                  SerializedVaultKeyset::SCRYPT_DERIVED |
                  SerializedVaultKeyset::ECC |
                  SerializedVaultKeyset::PCR_BOUND);
  keyset.SetTpmPublicKeyHash(brillo::SecureBlob("yadayada"));
  keyset.SetTPMKey(brillo::SecureBlob("blabla"));
  keyset.SetExtendedTPMKey(brillo::SecureBlob("foobaz"));
  keyset.password_rounds_ = 5;
  keyset.vkk_iv_ = brillo::SecureBlob("wowowow");
  keyset.auth_salt_ = brillo::SecureBlob("salt");

  AuthBlockState auth_state;
  EXPECT_TRUE(GetAuthBlockState(keyset, auth_state));

  const TpmEccAuthBlockState* tpm_state =
      std::get_if<TpmEccAuthBlockState>(&auth_state.state);

  ASSERT_NE(tpm_state, nullptr);
  EXPECT_TRUE(tpm_state->salt.has_value());
  EXPECT_TRUE(tpm_state->sealed_hvkkm.has_value());
  EXPECT_TRUE(tpm_state->extended_sealed_hvkkm.has_value());
  EXPECT_TRUE(tpm_state->tpm_public_key_hash.has_value());
  EXPECT_TRUE(tpm_state->vkk_iv.has_value());
  EXPECT_EQ(tpm_state->auth_value_rounds.value(), 5);
}

TEST_F(VaultKeysetTest, GetNotPcrBoundAuthBlockState) {
  VaultKeyset keyset;
  keyset.Initialize(&platform_, &crypto_);

  keyset.CreateFromFileSystemKeyset(FileSystemKeyset::CreateRandom());
  keyset.SetFlags(SerializedVaultKeyset::TPM_WRAPPED);
  keyset.SetTpmPublicKeyHash(brillo::SecureBlob("yadayada"));
  keyset.SetTPMKey(brillo::SecureBlob("blabla"));

  AuthBlockState auth_state;
  EXPECT_TRUE(GetAuthBlockState(keyset, auth_state));

  const TpmNotBoundToPcrAuthBlockState* tpm_state =
      std::get_if<TpmNotBoundToPcrAuthBlockState>(&auth_state.state);
  ASSERT_NE(tpm_state, nullptr);
  ASSERT_TRUE(tpm_state->scrypt_derived.has_value());
  EXPECT_FALSE(tpm_state->scrypt_derived.value());
  EXPECT_TRUE(tpm_state->tpm_key.has_value());
}

TEST_F(VaultKeysetTest, GetPinWeaverAuthBlockState) {
  VaultKeyset keyset;
  keyset.Initialize(&platform_, &crypto_);

  const uint64_t le_label = 012345;
  keyset.CreateFromFileSystemKeyset(FileSystemKeyset::CreateRandom());
  keyset.SetFlags(SerializedVaultKeyset::LE_CREDENTIAL);
  keyset.SetLELabel(le_label);

  AuthBlockState auth_state;
  EXPECT_TRUE(GetAuthBlockState(keyset, auth_state));

  const PinWeaverAuthBlockState* pin_auth_state =
      std::get_if<PinWeaverAuthBlockState>(&auth_state.state);
  ASSERT_NE(pin_auth_state, nullptr);
  EXPECT_TRUE(pin_auth_state->le_label.has_value());
  EXPECT_EQ(le_label, pin_auth_state->le_label.value());
}

TEST_F(VaultKeysetTest, GetChallengeCredentialAuthBlockState) {
  VaultKeyset keyset;
  keyset.Initialize(&platform_, &crypto_);

  keyset.CreateFromFileSystemKeyset(FileSystemKeyset::CreateRandom());
  keyset.SetFlags(SerializedVaultKeyset::SCRYPT_WRAPPED |
                  SerializedVaultKeyset::SIGNATURE_CHALLENGE_PROTECTED);

  AuthBlockState auth_state;
  EXPECT_TRUE(GetAuthBlockState(keyset, auth_state));

  const ChallengeCredentialAuthBlockState* cc_state =
      std::get_if<ChallengeCredentialAuthBlockState>(&auth_state.state);
  EXPECT_NE(cc_state, nullptr);
}

TEST_F(VaultKeysetTest, GetLibscryptCompatAuthBlockState) {
  VaultKeyset keyset;
  keyset.Initialize(&platform_, &crypto_);

  keyset.CreateFromFileSystemKeyset(FileSystemKeyset::CreateRandom());
  keyset.SetFlags(SerializedVaultKeyset::SCRYPT_WRAPPED);
  keyset.SetWrappedKeyset(brillo::SecureBlob("foo"));
  keyset.SetWrappedChapsKey(brillo::SecureBlob("bar"));
  keyset.SetWrappedResetSeed(brillo::SecureBlob("baz"));

  AuthBlockState auth_state;
  EXPECT_TRUE(GetAuthBlockState(keyset, auth_state));

  const LibScryptCompatAuthBlockState* scrypt_state =
      std::get_if<LibScryptCompatAuthBlockState>(&auth_state.state);
  ASSERT_NE(scrypt_state, nullptr);
  EXPECT_TRUE(scrypt_state->wrapped_keyset.has_value());
  EXPECT_TRUE(scrypt_state->wrapped_chaps_key.has_value());
  EXPECT_TRUE(scrypt_state->wrapped_reset_seed.has_value());
}

TEST_F(VaultKeysetTest, GetDoubleWrappedCompatAuthBlockStateFailure) {
  VaultKeyset keyset;
  keyset.Initialize(&platform_, &crypto_);

  keyset.CreateFromFileSystemKeyset(FileSystemKeyset::CreateRandom());
  keyset.SetFlags(SerializedVaultKeyset::SCRYPT_WRAPPED |
                  SerializedVaultKeyset::TPM_WRAPPED);

  AuthBlockState auth_state;

  // A required tpm_key is not set in keyset, failure in creating
  // sub-state TpmNotBoundToPcrAuthBlockState.
  EXPECT_FALSE(GetAuthBlockState(keyset, auth_state));

  const DoubleWrappedCompatAuthBlockState* double_wrapped_state =
      std::get_if<DoubleWrappedCompatAuthBlockState>(&auth_state.state);
  EXPECT_EQ(double_wrapped_state, nullptr);
}

TEST_F(VaultKeysetTest, GetDoubleWrappedCompatAuthBlockState) {
  VaultKeyset keyset;
  keyset.Initialize(&platform_, &crypto_);

  keyset.CreateFromFileSystemKeyset(FileSystemKeyset::CreateRandom());
  keyset.SetFlags(SerializedVaultKeyset::SCRYPT_WRAPPED |
                  SerializedVaultKeyset::TPM_WRAPPED);
  keyset.SetTPMKey(brillo::SecureBlob("blabla"));

  AuthBlockState auth_state;
  EXPECT_TRUE(GetAuthBlockState(keyset, auth_state));

  const DoubleWrappedCompatAuthBlockState* double_wrapped_state =
      std::get_if<DoubleWrappedCompatAuthBlockState>(&auth_state.state);
  EXPECT_NE(double_wrapped_state, nullptr);
}

TEST_F(VaultKeysetTest, EncryptionTest) {
  // Check that EncryptVaultKeyset returns something other than the bytes
  // passed.

  VaultKeyset vault_keyset;
  vault_keyset.Initialize(&platform_, &crypto_);
  vault_keyset.CreateFromFileSystemKeyset(FileSystemKeyset::CreateRandom());

  SecureBlob key(20);
  GetSecureRandom(key.data(), key.size());

  AuthBlockState auth_block_state;
  ASSERT_TRUE(vault_keyset.EncryptVaultKeyset(key, "", &auth_block_state));
}

TEST_F(VaultKeysetTest, DecryptionTest) {
  // Check that DecryptVaultKeyset returns the original keyset.

  VaultKeyset vault_keyset;
  vault_keyset.Initialize(&platform_, &crypto_);
  vault_keyset.CreateFromFileSystemKeyset(FileSystemKeyset::CreateRandom());

  SecureBlob key(20);
  GetSecureRandom(key.data(), key.size());

  AuthBlockState auth_block_state;
  ASSERT_TRUE(vault_keyset.EncryptVaultKeyset(key, "", &auth_block_state));

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

TEST_F(VaultKeysetTest, GetLegacyLabelTest) {
  VaultKeyset vault_keyset;
  vault_keyset.Initialize(&platform_, &crypto_);
  vault_keyset.SetLegacyIndex(kLegacyIndex);

  ASSERT_EQ(vault_keyset.GetLabel(), kLegacyLabel);
}

TEST_F(VaultKeysetTest, GetLabelTest) {
  VaultKeyset vault_keyset;
  vault_keyset.Initialize(&platform_, &crypto_);
  KeyData key_data;
  key_data.set_label(kTempLabel);
  vault_keyset.SetLegacyIndex(kLegacyIndex);
  vault_keyset.SetKeyData(key_data);

  ASSERT_EQ(vault_keyset.GetLabel(), kTempLabel);
}

TEST_F(VaultKeysetTest, GetEmptyLabelTest) {
  VaultKeyset vault_keyset;
  vault_keyset.Initialize(&platform_, &crypto_);
  KeyData key_data;

  // Setting empty label.
  key_data.set_label("");

  vault_keyset.SetLegacyIndex(kLegacyIndex);
  vault_keyset.SetKeyData(key_data);

  ASSERT_EQ(vault_keyset.GetLabel(), kLegacyLabel);
}

TEST_F(VaultKeysetTest, InitializeToAdd) {
  // Check if InitializeToAdd correctly copies keys
  // from parameter vault keyset to underlying data structure.

  VaultKeyset vault_keyset;
  vault_keyset.Initialize(&platform_, &crypto_);
  vault_keyset.CreateFromFileSystemKeyset(FileSystemKeyset::CreateRandom());

  const auto reset_iv = CreateSecureRandomBlob(kAesBlockSize);
  static const int kFscryptPolicyVersion = 2;
  vault_keyset.SetResetIV(reset_iv);
  vault_keyset.SetFSCryptPolicyVersion(kFscryptPolicyVersion);
  vault_keyset.SetLegacyIndex(kLegacyIndex);

  VaultKeyset vault_keyset_copy;
  vault_keyset_copy.InitializeToAdd(vault_keyset);

  SecureBlob key(kPasswordKey);
  std::string obfuscated_username(kObfuscatedUsername);
  vault_keyset.Encrypt(key, obfuscated_username);

  // Check that InitializeToAdd correctly copied vault_keyset fields
  // i.e. fek/fnek keys, reset seed, reset IV, and FSCrypt policy version
  // FEK
  ASSERT_EQ(vault_keyset.GetFek(), vault_keyset_copy.GetFek());
  ASSERT_EQ(vault_keyset.GetFekSig(), vault_keyset_copy.GetFekSig());
  ASSERT_EQ(vault_keyset.GetFekSalt(), vault_keyset_copy.GetFekSalt());
  // FNEK
  ASSERT_EQ(vault_keyset.GetFnek(), vault_keyset_copy.GetFnek());
  ASSERT_EQ(vault_keyset.GetFnekSig(), vault_keyset_copy.GetFnekSig());
  ASSERT_EQ(vault_keyset.GetFnekSalt(), vault_keyset_copy.GetFnekSalt());
  // Other metadata
  ASSERT_EQ(vault_keyset.GetResetSeed(), vault_keyset_copy.GetResetSeed());
  ASSERT_EQ(vault_keyset.GetResetIV(), vault_keyset_copy.GetResetIV());
  ASSERT_EQ(vault_keyset.GetChapsKey(), vault_keyset_copy.GetChapsKey());
  ASSERT_EQ(vault_keyset.GetFSCryptPolicyVersion(),
            vault_keyset_copy.GetFSCryptPolicyVersion());

  // Other fields are empty/not changed/uninitialized
  // i.e. the wrapped_keyset_ shouldn't be copied
  ASSERT_NE(vault_keyset.GetWrappedKeyset(),
            vault_keyset_copy.GetWrappedKeyset());
  // int32_t flags_
  ASSERT_NE(vault_keyset_copy.GetFlags(), vault_keyset.GetFlags());
  // int legacy_index_
  ASSERT_NE(vault_keyset_copy.GetLegacyIndex(), vault_keyset.GetLegacyIndex());
}

TEST_F(VaultKeysetTest, DecryptFailNotLoaded) {
  // Check to decrypt a VaultKeyset that hasn't been loaded yet.
  VaultKeyset vault_keyset;
  vault_keyset.Initialize(&platform_, &crypto_);
  vault_keyset.CreateFromFileSystemKeyset(FileSystemKeyset::CreateRandom());

  SecureBlob key(kPasswordKey);
  std::string obfuscated_username(kObfuscatedUsername);
  ASSERT_TRUE(vault_keyset.Encrypt(key, obfuscated_username));

  CryptoError crypto_error = CryptoError::CE_NONE;
  // locked_to_single_user determines whether to use the extended tmp_key,
  // uses normal tpm_key when false with a TpmBoundToPcrAuthBlock
  ASSERT_FALSE(vault_keyset.Decrypt(key, false /*locked_to_single_user*/,
                                    &crypto_error));
  ASSERT_EQ(crypto_error, CryptoError::CE_OTHER_CRYPTO);
}

TEST_F(VaultKeysetTest, DecryptTPMCommErr) {
  // Test to have Decrypt() fail because of CE_TPM_COMM_ERROR.
  // Setup
  auto mock_loader = std::make_unique<MockCryptohomeKeyLoader>();
  MockCryptohomeKeyLoader* mock_loader_ptr = mock_loader.get();
  std::vector<
      std::pair<CryptohomeKeyType, std::unique_ptr<CryptohomeKeyLoader>>>
      mock_loaders;
  mock_loaders.push_back(
      std::make_pair(CryptohomeKeyType::kRSA, std::move(mock_loader)));
  auto cryptohome_keys_manager =
      std::make_unique<CryptohomeKeysManager>(std::move(mock_loaders));

  crypto_.Init(&tpm_, cryptohome_keys_manager.get());

  EXPECT_CALL(tpm_, IsEnabled()).WillRepeatedly(Return(true));
  EXPECT_CALL(tpm_, IsOwned()).WillRepeatedly(Return(true));

  SecureBlob bytes;
  EXPECT_CALL(platform_, WriteFileAtomicDurable(FilePath(kFilePath), _, _))
      .WillOnce(WithArg<1>(CopyToSecureBlob(&bytes)));
  EXPECT_CALL(platform_, ReadFile(FilePath(kFilePath), _))
      .WillOnce(WithArg<1>(CopyFromSecureBlob(&bytes)));

  VaultKeyset vk;
  vk.Initialize(&platform_, &crypto_);
  vk.CreateFromFileSystemKeyset(FileSystemKeyset::CreateRandom());
  vk.SetFlags(SerializedVaultKeyset::TPM_WRAPPED);

  // Test
  SecureBlob key(kPasswordKey);
  std::string obfuscated_username(kObfuscatedUsername);
  ASSERT_TRUE(vk.Encrypt(key, obfuscated_username));
  ASSERT_TRUE(vk.Save(FilePath(kFilePath)));

  VaultKeyset new_keyset;
  new_keyset.Initialize(&platform_, &crypto_);
  EXPECT_TRUE(new_keyset.Load(FilePath(kFilePath)));

  EXPECT_CALL(*mock_loader_ptr, HasCryptohomeKey())
      .WillRepeatedly(Return(false));

  // DecryptVaultKeyset within Decrypt fails
  // and passes error CryptoError::CE_TPM_COMM_ERROR
  // Decrypt -> DecryptVaultKeyset -> Derive
  // -> CheckTPMReadiness -> HasCryptohomeKey(fails and error propagates up)
  CryptoError tmp_error;
  ASSERT_FALSE(new_keyset.Decrypt(key, false, &tmp_error));
  ASSERT_EQ(tmp_error, CryptoError::CE_TPM_COMM_ERROR);
}

TEST_F(VaultKeysetTest, LibScryptBackwardCompatibility) {
  VaultKeyset vk;
  vk.Initialize(&platform_, &crypto_);

  SerializedVaultKeyset serialized;
  serialized.ParseFromString(
      HexDecode(kHexLibScryptExampleSerializedVaultKeyset));

  vk.InitializeFromSerialized(serialized);

  // TODO(b/198394243): We should remove this because it's not actually used.
  EXPECT_EQ(SecureBlobToHex(vk.auth_salt_), kHexLibScryptExampleSalt);

  AuthBlockState auth_state;
  EXPECT_TRUE(GetAuthBlockState(vk, auth_state));

  CryptoError crypto_error = CryptoError::CE_NONE;
  EXPECT_TRUE(vk.DecryptVaultKeyset(
      brillo::SecureBlob(HexDecode(kHexLibScryptExamplePasskey)), false,
      &crypto_error));
  EXPECT_EQ(CryptoError::CE_NONE, crypto_error);

  EXPECT_EQ(SecureBlobToHex(vk.GetFek()), kHexLibScryptExampleFek);
  EXPECT_EQ(SecureBlobToHex(vk.GetFekSig()), kHexLibScryptExampleFekSig);
  EXPECT_EQ(SecureBlobToHex(vk.GetFekSalt()), kHexLibScryptExampleFekSalt);
  EXPECT_EQ(SecureBlobToHex(vk.GetFnek()), kHexLibScryptExampleFnek);
  EXPECT_EQ(SecureBlobToHex(vk.GetFnekSig()), kHexLibScryptExampleFnekSig);
  EXPECT_EQ(SecureBlobToHex(vk.GetFnekSalt()), kHexLibScryptExampleFnekSalt);
}

TEST_F(VaultKeysetTest, GetTpmWritePasswordRounds) {
  // Test to ensure that for GetTpmNotBoundtoPcrState
  // correctly copies the password_rounds field from
  // the VaultKeyset to the auth_state parameter.

  VaultKeyset keyset;
  SerializedVaultKeyset serialized_vk;
  serialized_vk.set_flags(SerializedVaultKeyset::TPM_WRAPPED);
  serialized_vk.set_password_rounds(kPasswordRounds);

  keyset.InitializeFromSerialized(serialized_vk);
  keyset.Initialize(&platform_, &crypto_);

  keyset.SetTPMKey(brillo::SecureBlob(kFakePasswordKey));

  AuthBlockState tpm_state;
  EXPECT_TRUE(GetAuthBlockState(keyset, tpm_state));
  auto test_state =
      std::get_if<TpmNotBoundToPcrAuthBlockState>(&tpm_state.state);
  // test_state is of type TpmNotBoundToPcrAuthBlockState
  ASSERT_EQ(keyset.GetPasswordRounds(), test_state->password_rounds.value());
}

TEST_F(VaultKeysetTest, DecryptionTestWithKeyBlobs) {
  // Check that Decrypt returns the original keyset.
  // Setup

  VaultKeyset vault_keyset;
  vault_keyset.Initialize(&platform_, &crypto_);
  vault_keyset.CreateFromFileSystemKeyset(FileSystemKeyset::CreateRandom());

  SecureBlob bytes;
  EXPECT_CALL(platform_, WriteFileAtomicDurable(FilePath(kFilePath), _, _))
      .WillOnce(WithArg<1>(CopyToSecureBlob(&bytes)));

  EXPECT_CALL(platform_, ReadFile(FilePath(kFilePath), _))
      .WillOnce(WithArg<1>(CopyFromSecureBlob(&bytes)));

  KeyBlobs key_blobs = {.vkk_key = brillo::SecureBlob(32, 'A'),
                        .vkk_iv = brillo::SecureBlob(16, 'B'),
                        .chaps_iv = brillo::SecureBlob(16, 'C')};

  TpmBoundToPcrAuthBlockState pcr_state = {.salt = brillo::SecureBlob("salt")};
  AuthBlockState auth_state = {.state = pcr_state};
  ASSERT_TRUE(vault_keyset.EncryptEx(key_blobs, auth_state));
  EXPECT_TRUE(vault_keyset.Save(FilePath(kFilePath)));

  SecureBlob original_data;
  ASSERT_TRUE(vault_keyset.ToKeysBlob(&original_data));

  // Test
  VaultKeyset new_keyset;
  new_keyset.Initialize(&platform_, &crypto_);
  EXPECT_TRUE(new_keyset.Load(FilePath(kFilePath)));
  CryptoError crypto_error = CryptoError::CE_NONE;
  ASSERT_TRUE(new_keyset.DecryptEx(key_blobs, &crypto_error));
  ASSERT_EQ(crypto_error, CryptoError::CE_NONE);

  // Verify
  SecureBlob new_data;
  ASSERT_TRUE(new_keyset.ToKeysBlob(&new_data));

  EXPECT_EQ(new_data.size(), original_data.size());
  ASSERT_TRUE(VaultKeysetTest::FindBlobInBlob(new_data, original_data));
}

TEST_F(VaultKeysetTest, DecryptWithAuthBlockFailNotLoaded) {
  // Check to decrypt a VaultKeyset that hasn't been loaded yet.

  VaultKeyset vault_keyset;
  vault_keyset.Initialize(&platform_, &crypto_);
  vault_keyset.CreateFromFileSystemKeyset(FileSystemKeyset::CreateRandom());

  KeyBlobs key_blobs = {.vkk_key = brillo::SecureBlob(32, 'A'),
                        .vkk_iv = brillo::SecureBlob(16, 'B'),
                        .chaps_iv = brillo::SecureBlob(16, 'C')};

  TpmBoundToPcrAuthBlockState pcr_state = {.salt = brillo::SecureBlob("salt")};
  AuthBlockState auth_state = {.state = pcr_state};

  EXPECT_TRUE(vault_keyset.EncryptEx(key_blobs, auth_state));

  CryptoError crypto_error = CryptoError::CE_NONE;
  // Load() needs to be called before decrypting the keyset.
  ASSERT_FALSE(vault_keyset.DecryptEx(key_blobs, &crypto_error));
  ASSERT_EQ(crypto_error, CryptoError::CE_OTHER_CRYPTO);
}

class LeCredentialsManagerTest : public ::testing::Test {
 public:
  LeCredentialsManagerTest() : crypto_(&platform_) {
    EXPECT_CALL(cryptohome_keys_manager_, Init())
        .WillOnce(Return());  // because HasCryptohomeKey returned false once.

    EXPECT_CALL(tpm_, IsEnabled()).WillRepeatedly(Return(true));
    EXPECT_CALL(tpm_, IsOwned()).WillRepeatedly(Return(true));

    // Raw pointer as crypto_ expects unique_ptr, which we will wrap this
    // allocation into.
    le_cred_manager_ = new MockLECredentialManager();
    EXPECT_CALL(*le_cred_manager_, CheckCredential(_, _, _, _))
        .WillRepeatedly(DoAll(
            SetArgPointee<2>(
                brillo::SecureBlob(HexDecode(kHexHighEntropySecret))),
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

  pin_vault_keyset_.CreateFromFileSystemKeyset(
      FileSystemKeyset::CreateRandom());
  pin_vault_keyset_.SetLowEntropyCredential(true);

  // This used to happen in VaultKeyset::EncryptVaultKeyset, but now happens in
  // VaultKeyset::Encrypt and thus needs to be done manually here.
  pin_vault_keyset_.reset_seed_ = CreateSecureRandomBlob(kAesBlockSize);
  pin_vault_keyset_.reset_salt_ = CreateSecureRandomBlob(kAesBlockSize);
  pin_vault_keyset_.reset_secret_ = HmacSha256(
      pin_vault_keyset_.reset_salt_.value(), pin_vault_keyset_.reset_seed_);

  AuthBlockState auth_block_state;
  EXPECT_TRUE(pin_vault_keyset_.EncryptVaultKeyset(
      brillo::SecureBlob(HexDecode(kHexVaultKey)), "unused",
      &auth_block_state));

  EXPECT_TRUE(
      std::holds_alternative<PinWeaverAuthBlockState>(auth_block_state.state));
}

TEST_F(LeCredentialsManagerTest, EncryptFail) {
  EXPECT_CALL(*le_cred_manager_, InsertCredential(_, _, _, _, _, _))
      .WillOnce(Return(LE_CRED_ERROR_NO_FREE_LABEL));

  pin_vault_keyset_.CreateFromFileSystemKeyset(
      FileSystemKeyset::CreateRandom());
  pin_vault_keyset_.SetLowEntropyCredential(true);

  // This used to happen in VaultKeyset::EncryptVaultKeyset, but now happens in
  // VaultKeyset::Encrypt and thus needs to be done manually here.
  pin_vault_keyset_.reset_seed_ = CreateSecureRandomBlob(kAesBlockSize);
  pin_vault_keyset_.reset_salt_ = CreateSecureRandomBlob(kAesBlockSize);
  pin_vault_keyset_.reset_secret_ = HmacSha256(
      pin_vault_keyset_.reset_salt_.value(), pin_vault_keyset_.reset_seed_);

  AuthBlockState auth_block_state;
  EXPECT_FALSE(pin_vault_keyset_.EncryptVaultKeyset(
      brillo::SecureBlob(HexDecode(kHexVaultKey)), "unused",
      &auth_block_state));
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
  EXPECT_TRUE(GetAuthBlockState(vk, auth_state));

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

  pin_vault_keyset_.CreateFromFileSystemKeyset(
      FileSystemKeyset::CreateRandom());
  pin_vault_keyset_.SetLowEntropyCredential(true);

  // This used to happen in VaultKeyset::EncryptVaultKeyset, but now happens in
  // VaultKeyset::Encrypt and thus needs to be done manually here.
  pin_vault_keyset_.reset_seed_ = CreateSecureRandomBlob(kAesBlockSize);
  pin_vault_keyset_.reset_salt_ = CreateSecureRandomBlob(kAesBlockSize);
  pin_vault_keyset_.reset_secret_ = HmacSha256(
      pin_vault_keyset_.reset_salt_.value(), pin_vault_keyset_.reset_seed_);
  pin_vault_keyset_.auth_locked_ = true;

  SecureBlob key(kPasswordKey);
  std::string obfuscated_username(kObfuscatedUsername);
  EXPECT_TRUE(pin_vault_keyset_.Encrypt(key, obfuscated_username));
  EXPECT_TRUE(pin_vault_keyset_.HasKeyData());
  EXPECT_FALSE(pin_vault_keyset_.auth_locked_);

  const SerializedVaultKeyset& serialized = pin_vault_keyset_.ToSerialized();
  EXPECT_FALSE(serialized.key_data().policy().auth_locked());
}

TEST_F(LeCredentialsManagerTest, DecryptTPMDefendLock) {
  // Test to have LECredential fail Decrypt because CE_TPM_DEFEND_LOCK
  // Setup
  pin_vault_keyset_.CreateFromFileSystemKeyset(
      FileSystemKeyset::CreateRandom());
  pin_vault_keyset_.SetLowEntropyCredential(true);

  SecureBlob bytes;
  EXPECT_CALL(platform_, WriteFileAtomicDurable(FilePath(kFilePath), _, _))
      .WillOnce(WithArg<1>(CopyToSecureBlob(&bytes)))
      .WillOnce(WithArg<1>(CopyToSecureBlob(&bytes)));

  EXPECT_CALL(platform_, ReadFile(FilePath(kFilePath), _))
      .WillOnce(WithArg<1>(CopyFromSecureBlob(&bytes)));

  SecureBlob key(kPasswordKey);
  std::string obfuscated_username(kObfuscatedUsername);
  ASSERT_TRUE(pin_vault_keyset_.Encrypt(key, obfuscated_username));
  ASSERT_TRUE(pin_vault_keyset_.Save(FilePath(kFilePath)));

  VaultKeyset new_keyset;
  new_keyset.Initialize(&platform_, &crypto_);
  EXPECT_TRUE(new_keyset.Load(FilePath(kFilePath)));

  // Test
  ASSERT_FALSE(new_keyset.GetAuthLocked());
  // Have le_cred_manager inject a
  // CryptoError::CE_TPM_DEFEND_LOCK error
  EXPECT_CALL(*le_cred_manager_, CheckCredential(_, _, _, _))
      .WillOnce(Return(LE_CRED_ERROR_TOO_MANY_ATTEMPTS));

  CryptoError crypto_error;
  ASSERT_FALSE(new_keyset.Decrypt(key, false, &crypto_error));
  ASSERT_EQ(crypto_error, CryptoError::CE_TPM_DEFEND_LOCK);
  ASSERT_TRUE(new_keyset.GetAuthLocked());
}

TEST_F(LeCredentialsManagerTest, EncryptWithKeyBlobs) {
  EXPECT_CALL(*le_cred_manager_, InsertCredential(_, _, _, _, _, _))
      .WillOnce(Return(LE_CRED_SUCCESS));

  pin_vault_keyset_.CreateFromFileSystemKeyset(
      FileSystemKeyset::CreateRandom());
  pin_vault_keyset_.SetLowEntropyCredential(true);

  std::optional<brillo::SecureBlob> reset_secret =
      pin_vault_keyset_.GetOrGenerateResetSecret();
  EXPECT_TRUE(reset_secret.has_value());

  auto auth_block = std::make_unique<PinWeaverAuthBlock>(
      crypto_.le_manager(), crypto_.cryptohome_keys_manager());

  AuthInput auth_input = {brillo::SecureBlob(HexDecode(kHexVaultKey)), false,
                          "unused", reset_secret.value()};
  KeyBlobs key_blobs;
  AuthBlockState auth_state;
  auth_block->Create(auth_input, &auth_state, &key_blobs);

  EXPECT_TRUE(
      std::holds_alternative<PinWeaverAuthBlockState>(auth_state.state));
  EXPECT_TRUE(pin_vault_keyset_.EncryptEx(key_blobs, auth_state));
}

TEST_F(LeCredentialsManagerTest, EncryptWithKeyBlobsFailWithBadAuthState) {
  EXPECT_CALL(*le_cred_manager_, InsertCredential(_, _, _, _, _, _))
      .WillOnce(Return(LE_CRED_ERROR_NO_FREE_LABEL));

  pin_vault_keyset_.CreateFromFileSystemKeyset(
      FileSystemKeyset::CreateRandom());
  pin_vault_keyset_.SetLowEntropyCredential(true);

  brillo::SecureBlob reset_secret =
      pin_vault_keyset_.GetOrGenerateResetSecret().value();

  auto auth_block = std::make_unique<PinWeaverAuthBlock>(
      crypto_.le_manager(), crypto_.cryptohome_keys_manager());

  AuthInput auth_input = {brillo::SecureBlob(44, 'A'), false, "unused",
                          reset_secret};
  KeyBlobs key_blobs;
  AuthBlockState auth_state;
  auth_block->Create(auth_input, &auth_state, &key_blobs);

  EXPECT_FALSE(
      std::holds_alternative<PinWeaverAuthBlockState>(auth_state.state));
}

TEST_F(LeCredentialsManagerTest, DecryptWithKeyBlobs) {
  VaultKeyset vk;
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

  auto auth_block = std::make_unique<PinWeaverAuthBlock>(
      crypto_.le_manager(), crypto_.cryptohome_keys_manager());

  AuthInput auth_input = {brillo::SecureBlob(HexDecode(kHexVaultKey)), false};
  KeyBlobs key_blobs;
  AuthBlockState auth_state;
  EXPECT_TRUE(vk.GetPinWeaverState(&auth_state));
  auth_block->Derive(auth_input, auth_state, &key_blobs);

  CryptoError crypto_error = CryptoError::CE_NONE;
  EXPECT_TRUE(vk.DecryptVaultKeysetEx(key_blobs, &crypto_error));
  EXPECT_EQ(CryptoError::CE_NONE, crypto_error);
}

}  // namespace cryptohome
