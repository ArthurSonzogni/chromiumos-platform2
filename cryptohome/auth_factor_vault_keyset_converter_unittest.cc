
// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_factor_vault_keyset_converter.h"

#include <stdint.h>

#include <map>
#include <memory>
#include <vector>

#include <base/check.h>
#include <brillo/cryptohome.h>
#include <cryptohome/proto_bindings/UserDataAuth.pb.h>
#include <cryptohome/proto_bindings/auth_factor.pb.h>
#include <cryptohome/proto_bindings/rpc.pb.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <libhwsec-foundation/crypto/secure_blob_util.h>

#include "cryptohome/auth_factor/auth_factor.h"
#include "cryptohome/auth_factor/auth_factor_label.h"
#include "cryptohome/auth_factor/auth_factor_type.h"
#include "cryptohome/credentials.h"
#include "cryptohome/crypto.h"
#include "cryptohome/filesystem_layout.h"
#include "cryptohome/key_objects.h"
#include "cryptohome/keyset_management.h"
#include "cryptohome/mock_crypto.h"
#include "cryptohome/mock_platform.h"
#include "cryptohome/mock_tpm.h"
#include "cryptohome/vault_keyset.h"
#include "cryptohome/vault_keyset.pb.h"

using ::testing::NiceMock;

namespace {
constexpr char kUsername[] = "user";
constexpr char kLabel[] = "label0";
constexpr char kLabel1[] = "label1";
constexpr char kLabel2[] = "label2";
constexpr char kUserPassword[] = "user_pass";

constexpr char kFirstIndice[] = "0";
constexpr char kSecondIndice[] = "1";
constexpr char kThirdIndice[] = "2";

}  // namespace

namespace cryptohome {

class AuthFactorVaultKeysetConverterTest : public ::testing::Test {
 public:
  AuthFactorVaultKeysetConverterTest() : crypto_(&platform_) {}

  ~AuthFactorVaultKeysetConverterTest() override {}

  // Not copyable or movable
  AuthFactorVaultKeysetConverterTest(
      const AuthFactorVaultKeysetConverterTest&) = delete;
  AuthFactorVaultKeysetConverterTest& operator=(
      const AuthFactorVaultKeysetConverterTest&) = delete;
  AuthFactorVaultKeysetConverterTest(AuthFactorVaultKeysetConverterTest&&) =
      delete;
  AuthFactorVaultKeysetConverterTest& operator=(
      AuthFactorVaultKeysetConverterTest&&) = delete;

  void SetUp() override {
    // Setup salt for brillo functions.
    keyset_management_ = std::make_unique<KeysetManagement>(
        &platform_, &crypto_, std::make_unique<VaultKeysetFactory>());
    converter_ = std::make_unique<AuthFactorVaultKeysetConverter>(
        keyset_management_.get());
    file_system_keyset_ = FileSystemKeyset::CreateRandom();

    AddUser(kUserPassword);

    PrepareDirectoryStructure();
  }

 protected:
  NiceMock<MockPlatform> platform_;
  Crypto crypto_;
  FileSystemKeyset file_system_keyset_;
  std::unique_ptr<KeysetManagement> keyset_management_;
  std::unique_ptr<AuthFactorVaultKeysetConverter> converter_;
  struct UserInfo {
    std::string name;
    std::string obfuscated;
    brillo::SecureBlob passkey;
    Credentials credentials;
    base::FilePath homedir_path;
    base::FilePath user_path;
  };

  UserInfo user;

  void AddUser(const char* password) {
    std::string obfuscated =
        brillo::cryptohome::home::SanitizeUserName(kUsername);
    brillo::SecureBlob passkey(password);
    Credentials credentials(kUsername, passkey);

    user = {kUsername,
            obfuscated,
            passkey,
            credentials,
            UserPath(obfuscated),
            brillo::cryptohome::home::GetHashedUserPath(obfuscated)};
  }

  void PrepareDirectoryStructure() {
    ASSERT_TRUE(platform_.CreateDirectory(ShadowRoot()));
    ASSERT_TRUE(platform_.CreateDirectory(
        brillo::cryptohome::home::GetUserPathPrefix()));
    // We only need the homedir path, not the vault/mount paths.
    ASSERT_TRUE(platform_.CreateDirectory(user.homedir_path));
  }

  KeyData SetKeyData(const std::string& label) {
    KeyData key_data;
    key_data.set_label(label);
    return key_data;
  }

  void KeysetSetUpWithKeyData(const KeyData& key_data,
                              const std::string& indice) {
    VaultKeyset vk;
    vk.Initialize(&platform_, &crypto_);
    vk.CreateFromFileSystemKeyset(file_system_keyset_);
    vk.SetKeyData(key_data);
    user.credentials.set_key_data(key_data);
    ASSERT_TRUE(vk.Encrypt(user.passkey, user.obfuscated));
    ASSERT_TRUE(
        vk.Save(user.homedir_path.Append(kKeyFile).AddExtension(indice)));
  }
};

// Test that VaultKeysetsToAuthFactors return correct error when there is
// no VaultKeyset on the disk.
TEST_F(AuthFactorVaultKeysetConverterTest,
       ConvertToAuthFactorFailWhenListEmpty) {
  std::map<std::string, std::unique_ptr<AuthFactor>> label_to_auth_factor;
  EXPECT_EQ(
      user_data_auth::CRYPTOHOME_ERROR_KEY_NOT_FOUND,
      converter_->VaultKeysetsToAuthFactors(kUsername, label_to_auth_factor));
  EXPECT_TRUE(label_to_auth_factor.empty());
}

// Test that VaultKeysetsToAuthFactors lists the existing VaultKeyset on
// the disk.
TEST_F(AuthFactorVaultKeysetConverterTest, ConvertToAuthFactorListSuccess) {
  KeysetSetUpWithKeyData(SetKeyData(kLabel), kFirstIndice);
  std::map<std::string, std::unique_ptr<AuthFactor>> label_to_auth_factor;

  EXPECT_EQ(
      user_data_auth::CRYPTOHOME_ERROR_NOT_SET,
      converter_->VaultKeysetsToAuthFactors(kUsername, label_to_auth_factor));
  EXPECT_FALSE(label_to_auth_factor.empty());
  EXPECT_EQ(kLabel, label_to_auth_factor[kLabel]->label());
  EXPECT_EQ(AuthFactorType::kPassword, label_to_auth_factor[kLabel]->type());
}

// Test that VaultKeysetsToAuthFactors lists all the VaultKeysets in the
// disk.
TEST_F(AuthFactorVaultKeysetConverterTest,
       ConvertToAuthFactorListMultipleVaultKeysetsSuccess) {
  KeysetSetUpWithKeyData(SetKeyData(kLabel), kFirstIndice);
  KeysetSetUpWithKeyData(SetKeyData(kLabel1), kSecondIndice);
  KeysetSetUpWithKeyData(SetKeyData(kLabel2), kThirdIndice);

  std::map<std::string, std::unique_ptr<AuthFactor>> label_to_auth_factor;

  EXPECT_EQ(
      user_data_auth::CRYPTOHOME_ERROR_NOT_SET,
      converter_->VaultKeysetsToAuthFactors(kUsername, label_to_auth_factor));
  EXPECT_EQ(3, label_to_auth_factor.size());

  EXPECT_EQ(kLabel, label_to_auth_factor[kLabel]->label());
  EXPECT_EQ(AuthFactorType::kPassword, label_to_auth_factor[kLabel]->type());

  EXPECT_EQ(kLabel1, label_to_auth_factor[kLabel1]->label());
  EXPECT_EQ(AuthFactorType::kPassword, label_to_auth_factor[kLabel1]->type());

  EXPECT_EQ(kLabel2, label_to_auth_factor[kLabel2]->label());
  EXPECT_EQ(AuthFactorType::kPassword, label_to_auth_factor[kLabel2]->type());
}

// Test that PopulateKeyDataForVK returns correct KeyData for the given
// label.
TEST_F(AuthFactorVaultKeysetConverterTest, ConvertToVaultKeysetDataSuccess) {
  KeyData test_key_data = SetKeyData(kLabel);
  KeysetSetUpWithKeyData(test_key_data, kFirstIndice);

  KeyData key_data;
  std::string auth_factor_label = kLabel;
  EXPECT_EQ(
      user_data_auth::CRYPTOHOME_ERROR_NOT_SET,
      converter_->PopulateKeyDataForVK(kUsername, auth_factor_label, key_data));
  EXPECT_EQ(kLabel, key_data.label());
}

// Test that PopulateKeyDataForVK returns correct KeyData for the given
// label.
TEST_F(AuthFactorVaultKeysetConverterTest, ConvertToVaultKeysetDataFail) {
  KeyData test_key_data = SetKeyData(kLabel);
  KeysetSetUpWithKeyData(test_key_data, kFirstIndice);

  KeyData key_data;
  std::string auth_factor_label = kLabel1;
  EXPECT_EQ(
      user_data_auth::CRYPTOHOME_ERROR_KEY_NOT_FOUND,
      converter_->PopulateKeyDataForVK(kUsername, auth_factor_label, key_data));
}

}  // namespace cryptohome
