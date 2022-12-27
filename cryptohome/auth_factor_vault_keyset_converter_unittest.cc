
// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_factor_vault_keyset_converter.h"

#include <stdint.h>

#include <map>
#include <memory>
#include <vector>

#include <base/check.h>
#include <base/files/scoped_temp_dir.h>
#include <brillo/cryptohome.h>
#include <cryptohome/proto_bindings/UserDataAuth.pb.h>
#include <cryptohome/proto_bindings/auth_factor.pb.h>
#include <cryptohome/proto_bindings/key.pb.h>
#include <cryptohome/proto_bindings/rpc.pb.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <libhwsec/factory/tpm2_simulator_factory_for_test.h>
#include <libhwsec/frontend/cryptohome/mock_frontend.h>
#include <libhwsec/frontend/pinweaver/frontend.h>
#include <libhwsec/frontend/pinweaver/mock_frontend.h>
#include <libhwsec-foundation/crypto/secure_blob_util.h>
#include <libhwsec-foundation/error/testing_helper.h>

#include "cryptohome/auth_factor/auth_factor.h"
#include "cryptohome/auth_factor/auth_factor_metadata.h"
#include "cryptohome/auth_factor/auth_factor_type.h"
#include "cryptohome/credentials.h"
#include "cryptohome/crypto.h"
#include "cryptohome/filesystem_layout.h"
#include "cryptohome/keyset_management.h"
#include "cryptohome/le_credential_manager_impl.h"
#include "cryptohome/mock_cryptohome_keys_manager.h"
#include "cryptohome/mock_platform.h"
#include "cryptohome/vault_keyset.h"
#include "cryptohome/vault_keyset.pb.h"

namespace cryptohome {
namespace {

using ::hwsec_foundation::error::testing::ReturnError;
using ::hwsec_foundation::error::testing::ReturnOk;
using ::hwsec_foundation::error::testing::ReturnValue;
using ::testing::NiceMock;

constexpr char kUsername[] = "user";
constexpr char kPinLabel[] = "pin";
constexpr char kLabel0[] = "label0";
constexpr char kLabel1[] = "label1";
constexpr char kLabel2[] = "label2";
constexpr char kUserPassword[] = "user_pass";
constexpr char kCredsDir[] = "le_creds";

constexpr char kFirstIndex[] = "0";
constexpr char kSecondIndex[] = "1";
constexpr char kThirdIndex[] = "2";

class AuthFactorVaultKeysetConverterTest : public ::testing::Test {
 public:
  AuthFactorVaultKeysetConverterTest()
      : pinweaver_(tpm2_factory_.GetPinWeaverFrontend()),
        crypto_(&hwsec_, pinweaver_.get(), &cryptohome_keys_manager_, nullptr) {
  }

  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());

    crypto_.set_le_manager_for_testing(
        std::make_unique<LECredentialManagerImpl>(
            pinweaver_.get(), temp_dir_.GetPath().Append(kCredsDir)));
    crypto_.Init();

    EXPECT_CALL(hwsec_, IsEnabled()).WillRepeatedly(ReturnValue(true));
    EXPECT_CALL(hwsec_, IsReady()).WillRepeatedly(ReturnValue(true));
    EXPECT_CALL(hwsec_, IsSealingSupported()).WillRepeatedly(ReturnValue(true));
    EXPECT_CALL(hwsec_, GetManufacturer())
        .WillRepeatedly(ReturnValue(0x43524f53));
    EXPECT_CALL(hwsec_, GetAuthValue(_, _))
        .WillRepeatedly(ReturnValue(brillo::SecureBlob()));
    EXPECT_CALL(hwsec_, SealWithCurrentUser(_, _, _))
        .WillRepeatedly(ReturnValue(brillo::Blob()));
    EXPECT_CALL(hwsec_, GetPubkeyHash(_))
        .WillRepeatedly(ReturnValue(brillo::Blob()));

    keyset_management_ = std::make_unique<KeysetManagement>(
        &platform_, &crypto_, std::make_unique<VaultKeysetFactory>());
    converter_ = std::make_unique<AuthFactorVaultKeysetConverter>(
        keyset_management_.get());
    file_system_keyset_ = FileSystemKeyset::CreateRandom();

    AddUser(kUserPassword);

    PrepareDirectoryStructure();
    key_label_data_.clear();
    label_to_auth_factor_.clear();
    label_to_auth_factor_backup_.clear();
  }

 protected:
  NiceMock<MockPlatform> platform_;
  base::ScopedTempDir temp_dir_;

  NiceMock<hwsec::MockCryptohomeFrontend> hwsec_;
  hwsec::Tpm2SimulatorFactoryForTest tpm2_factory_;
  std::unique_ptr<hwsec::PinWeaverFrontend> pinweaver_;
  NiceMock<MockCryptohomeKeysManager> cryptohome_keys_manager_;
  Crypto crypto_;
  FileSystemKeyset file_system_keyset_;
  std::unique_ptr<KeysetManagement> keyset_management_;
  std::unique_ptr<AuthFactorVaultKeysetConverter> converter_;
  std::map<std::string, std::unique_ptr<AuthFactor>> label_to_auth_factor_;
  std::map<std::string, std::unique_ptr<AuthFactor>>
      label_to_auth_factor_backup_;
  std::map<std::string, KeyData> key_label_data_;
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

  static KeyData SetKeyData(const std::string& label) {
    KeyData key_data;
    key_data.set_label(label);
    return key_data;
  }

  static KeyData SetPinKeyData(const std::string& label) {
    KeyData key_data;
    key_data.set_type(KeyData::KEY_TYPE_PASSWORD);
    key_data.set_label(label);
    key_data.mutable_policy()->set_low_entropy_credential(true);
    return key_data;
  }

  static KeyData SetKioskKeyData(const std::string& label) {
    KeyData key_data;
    key_data.set_type(KeyData::KEY_TYPE_KIOSK);
    key_data.set_label(label);
    return key_data;
  }

  void BackupKeysetSetUpWithKeyData(const KeyData& key_data,
                                    const std::string& index) {
    VaultKeyset vk;
    vk.Initialize(&platform_, &crypto_);
    vk.CreateFromFileSystemKeyset(file_system_keyset_);
    vk.SetKeyData(key_data);
    vk.set_backup_vk_for_testing(true);
    user.credentials.set_key_data(key_data);
    ASSERT_TRUE(vk.Encrypt(user.passkey, user.obfuscated).ok());
    ASSERT_TRUE(
        vk.Save(user.homedir_path.Append(kKeyFile).AddExtension(index)));
  }

  void KeysetSetUpWithKeyData(const KeyData& key_data,
                              const std::string& index) {
    VaultKeyset vk;
    vk.Initialize(&platform_, &crypto_);
    vk.CreateFromFileSystemKeyset(file_system_keyset_);
    vk.SetKeyData(key_data);
    user.credentials.set_key_data(key_data);
    ASSERT_TRUE(vk.Encrypt(user.passkey, user.obfuscated).ok());
    ASSERT_TRUE(
        vk.Save(user.homedir_path.Append(kKeyFile).AddExtension(index)));
  }
};

// Test that VaultKeysetsToAuthFactorsAndKeyLabelData return correct error when
// there is no VaultKeyset on the disk.
TEST_F(AuthFactorVaultKeysetConverterTest,
       ConvertToAuthFactorFailWhenListEmpty) {
  EXPECT_EQ(user_data_auth::CRYPTOHOME_ERROR_KEY_NOT_FOUND,
            converter_->VaultKeysetsToAuthFactorsAndKeyLabelData(
                user.obfuscated, label_to_auth_factor_,
                label_to_auth_factor_backup_, &key_label_data_));
  EXPECT_TRUE(label_to_auth_factor_.empty());
  EXPECT_TRUE(label_to_auth_factor_backup_.empty());
  EXPECT_TRUE(label_to_auth_factor_.empty());
  EXPECT_TRUE(key_label_data_.empty());
}

// Test that VaultKeysetsToAuthFactorsAndKeyLabelData lists the existing
// VaultKeyset on the disk.
TEST_F(AuthFactorVaultKeysetConverterTest, ConvertToAuthFactorListSuccess) {
  KeysetSetUpWithKeyData(SetKeyData(kLabel0), kFirstIndex);
  EXPECT_EQ(user_data_auth::CRYPTOHOME_ERROR_NOT_SET,
            converter_->VaultKeysetsToAuthFactorsAndKeyLabelData(
                user.obfuscated, label_to_auth_factor_,
                label_to_auth_factor_backup_, &key_label_data_));

  EXPECT_FALSE(label_to_auth_factor_.empty());
  EXPECT_EQ(kLabel0, label_to_auth_factor_[kLabel0]->label());
  EXPECT_EQ(AuthFactorType::kPassword, label_to_auth_factor_[kLabel0]->type());
  EXPECT_TRUE(label_to_auth_factor_backup_.empty());
  EXPECT_FALSE(key_label_data_.empty());
  EXPECT_EQ(kLabel0, key_label_data_[kLabel0].label());
  EXPECT_EQ(KeyData::KEY_TYPE_PASSWORD, key_label_data_[kLabel0].type());
}

// Test that VaultKeysetsToAuthFactorsAndKeyLabelData lists all the VaultKeysets
// in the disk.
TEST_F(AuthFactorVaultKeysetConverterTest,
       ConvertToAuthFactorListMultipleVaultKeysetsSuccess) {
  KeysetSetUpWithKeyData(SetKeyData(kLabel0), kFirstIndex);
  KeysetSetUpWithKeyData(SetKeyData(kLabel1), kSecondIndex);
  KeysetSetUpWithKeyData(SetPinKeyData(kLabel2), kThirdIndex);

  EXPECT_EQ(user_data_auth::CRYPTOHOME_ERROR_NOT_SET,
            converter_->VaultKeysetsToAuthFactorsAndKeyLabelData(
                user.obfuscated, label_to_auth_factor_,
                label_to_auth_factor_backup_, &key_label_data_));

  EXPECT_EQ(3, label_to_auth_factor_.size());

  EXPECT_EQ(kLabel0, label_to_auth_factor_[kLabel0]->label());

  EXPECT_EQ(kLabel1, label_to_auth_factor_[kLabel1]->label());
  EXPECT_EQ(AuthFactorType::kPassword, label_to_auth_factor_[kLabel1]->type());

  EXPECT_EQ(kLabel2, label_to_auth_factor_[kLabel2]->label());
  EXPECT_EQ(AuthFactorType::kPin, label_to_auth_factor_[kLabel2]->type());

  EXPECT_EQ(3, key_label_data_.size());

  EXPECT_EQ(kLabel0, key_label_data_[kLabel0].label());
  EXPECT_EQ(KeyData::KEY_TYPE_PASSWORD, key_label_data_[kLabel0].type());

  EXPECT_EQ(kLabel1, key_label_data_[kLabel1].label());
  EXPECT_EQ(KeyData::KEY_TYPE_PASSWORD, key_label_data_[kLabel1].type());

  EXPECT_EQ(kLabel2, key_label_data_[kLabel2].label());
  EXPECT_EQ(KeyData::KEY_TYPE_PASSWORD, key_label_data_[kLabel2].type());
  EXPECT_TRUE(key_label_data_[kLabel2].has_policy());
  EXPECT_TRUE(key_label_data_[kLabel2].policy().low_entropy_credential());

  EXPECT_TRUE(label_to_auth_factor_backup_.empty());
}

// Test that PopulateKeyDataForVK returns correct KeyData for the given
// label.
TEST_F(AuthFactorVaultKeysetConverterTest, ConvertToVaultKeysetDataSuccess) {
  KeyData test_key_data = SetKeyData(kLabel0);
  KeysetSetUpWithKeyData(test_key_data, kFirstIndex);

  KeyData key_data;
  std::string auth_factor_label = kLabel0;
  EXPECT_EQ(user_data_auth::CRYPTOHOME_ERROR_NOT_SET,
            converter_->PopulateKeyDataForVK(user.obfuscated, auth_factor_label,
                                             key_data));
  EXPECT_EQ(kLabel0, key_data.label());
}

// Test that PopulateKeyDataForVK fails to return KeyData for a wrong given
// label.
TEST_F(AuthFactorVaultKeysetConverterTest, ConvertToVaultKeysetDataFail) {
  KeyData test_key_data = SetKeyData(kLabel0);
  KeysetSetUpWithKeyData(test_key_data, kFirstIndex);

  KeyData key_data;
  std::string auth_factor_label = kLabel1;
  EXPECT_EQ(user_data_auth::CRYPTOHOME_ERROR_KEY_NOT_FOUND,
            converter_->PopulateKeyDataForVK(user.obfuscated, auth_factor_label,
                                             key_data));
}

// Test that AuthFactorToKeyData generates correct KeyData for the given
// password label and type.
TEST_F(AuthFactorVaultKeysetConverterTest, GenerateKeyDataPassword) {
  KeyData key_data = SetKeyData(kLabel0);
  key_data.set_type(KeyData::KEY_TYPE_PASSWORD);

  KeyData test_key_data;
  std::string auth_factor_label = kLabel0;
  AuthFactorType auth_factor_type = AuthFactorType::kPassword;

  AuthFactorMetadata auth_factor_metadata;
  EXPECT_EQ(
      user_data_auth::CRYPTOHOME_ERROR_NOT_SET,
      converter_->AuthFactorToKeyData(auth_factor_label, auth_factor_type,
                                      auth_factor_metadata, test_key_data));
  EXPECT_EQ(key_data.label(), test_key_data.label());
  EXPECT_EQ(key_data.type(), test_key_data.type());
  EXPECT_FALSE(test_key_data.policy().low_entropy_credential());
}

// Test that AuthFactorToKeyData generates correct KeyData for the given
// pin factor and password type.
TEST_F(AuthFactorVaultKeysetConverterTest, GenerateKeyDataPin) {
  KeyData key_data = SetPinKeyData(kPinLabel);

  KeyData test_key_data;
  std::string auth_factor_label = kPinLabel;
  AuthFactorType auth_factor_type = AuthFactorType::kPin;

  AuthFactorMetadata auth_factor_metadata;
  EXPECT_EQ(
      user_data_auth::CRYPTOHOME_ERROR_NOT_SET,
      converter_->AuthFactorToKeyData(auth_factor_label, auth_factor_type,
                                      auth_factor_metadata, test_key_data));
  EXPECT_EQ(key_data.label(), test_key_data.label());
  EXPECT_EQ(key_data.type(), test_key_data.type());
  EXPECT_TRUE(test_key_data.policy().low_entropy_credential());
}

// Test that VaultKeysetToAuthFactor returns correct AuthFactor for the given
// label.
TEST_F(AuthFactorVaultKeysetConverterTest, VaultKeysetToAuthFactorSuccess) {
  KeyData test_key_data = SetKeyData(kLabel0);
  KeysetSetUpWithKeyData(test_key_data, kFirstIndex);

  KeyData key_data;
  std::string auth_factor_label = kLabel0;
  std::unique_ptr<AuthFactor> auth_factor =
      converter_->VaultKeysetToAuthFactor(user.obfuscated, auth_factor_label);
  EXPECT_NE(nullptr, auth_factor);
  EXPECT_EQ(kLabel0, auth_factor->label());
  EXPECT_EQ(AuthFactorType::kPassword, auth_factor->type());
}

// Test that VaultKeysetsToAuthFactorsAndKeyLabelData lists all the VaultKeysets
// in the disk.
TEST_F(AuthFactorVaultKeysetConverterTest, ConvertToAuthFactorListKiosk) {
  KeysetSetUpWithKeyData(SetKioskKeyData(kLabel0), kFirstIndex);
  EXPECT_EQ(user_data_auth::CRYPTOHOME_ERROR_NOT_SET,
            converter_->VaultKeysetsToAuthFactorsAndKeyLabelData(
                user.obfuscated, label_to_auth_factor_,
                label_to_auth_factor_backup_, &key_label_data_));

  EXPECT_EQ(1, label_to_auth_factor_.size());

  EXPECT_EQ(kLabel0, label_to_auth_factor_[kLabel0]->label());
  EXPECT_EQ(AuthFactorType::kKiosk, label_to_auth_factor_[kLabel0]->type());

  EXPECT_EQ(1, key_label_data_.size());

  EXPECT_EQ(kLabel0, key_label_data_[kLabel0].label());
  EXPECT_EQ(KeyData::KEY_TYPE_KIOSK, key_label_data_[kLabel0].type());
  EXPECT_TRUE(label_to_auth_factor_backup_.empty());
}

// Test that VaultKeysetsToAuthFactors lists all the backup VaultKeysets in
// label_to_auth_factor_backup_ and doesn't list any VaultKeysets in
// label_to_auth_factor_.
TEST_F(AuthFactorVaultKeysetConverterTest,
       ConvertToAuthFactorListsOnlyBackupVKs) {
  BackupKeysetSetUpWithKeyData(SetKeyData(kLabel0), kFirstIndex);
  BackupKeysetSetUpWithKeyData(SetKeyData(kLabel1), kSecondIndex);
  BackupKeysetSetUpWithKeyData(SetPinKeyData(kLabel2), kThirdIndex);

  EXPECT_EQ(user_data_auth::CRYPTOHOME_ERROR_NOT_SET,
            converter_->VaultKeysetsToAuthFactorsAndKeyLabelData(
                user.obfuscated, label_to_auth_factor_,
                label_to_auth_factor_backup_, nullptr));

  EXPECT_TRUE(label_to_auth_factor_.empty());
  EXPECT_EQ(3, label_to_auth_factor_backup_.size());

  EXPECT_EQ(kLabel0, label_to_auth_factor_backup_[kLabel0]->label());
  EXPECT_EQ(AuthFactorType::kPassword,
            label_to_auth_factor_backup_[kLabel0]->type());

  EXPECT_EQ(kLabel1, label_to_auth_factor_backup_[kLabel1]->label());
  EXPECT_EQ(AuthFactorType::kPassword,
            label_to_auth_factor_backup_[kLabel1]->type());

  EXPECT_EQ(kLabel2, label_to_auth_factor_backup_[kLabel2]->label());
  EXPECT_EQ(AuthFactorType::kPin,
            label_to_auth_factor_backup_[kLabel2]->type());
}

// Test that VaultKeysetsToAuthFactors includes backup VaultKeysets in correct
// lists.
TEST_F(AuthFactorVaultKeysetConverterTest,
       ConvertToAuthFactorIncludeBackupVKs) {
  KeysetSetUpWithKeyData(SetKeyData(kLabel0), kFirstIndex);
  BackupKeysetSetUpWithKeyData(SetKeyData(kLabel1), kSecondIndex);
  BackupKeysetSetUpWithKeyData(SetPinKeyData(kLabel2), kThirdIndex);

  EXPECT_EQ(user_data_auth::CRYPTOHOME_ERROR_NOT_SET,
            converter_->VaultKeysetsToAuthFactorsAndKeyLabelData(
                user.obfuscated, label_to_auth_factor_,
                label_to_auth_factor_backup_, nullptr));
  EXPECT_EQ(1, label_to_auth_factor_.size());
  EXPECT_EQ(2, label_to_auth_factor_backup_.size());

  EXPECT_EQ(kLabel0, label_to_auth_factor_[kLabel0]->label());
  EXPECT_EQ(AuthFactorType::kPassword, label_to_auth_factor_[kLabel0]->type());

  EXPECT_EQ(kLabel1, label_to_auth_factor_backup_[kLabel1]->label());
  EXPECT_EQ(AuthFactorType::kPassword,
            label_to_auth_factor_backup_[kLabel1]->type());

  EXPECT_EQ(kLabel2, label_to_auth_factor_backup_[kLabel2]->label());
  EXPECT_EQ(AuthFactorType::kPin,
            label_to_auth_factor_backup_[kLabel2]->type());
}

}  // namespace
}  // namespace cryptohome
