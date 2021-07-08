// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/cryptohome_ecc_key_loader.h"

#include <map>
#include <string>

#include <base/files/file_path.h>
#include <brillo/secure_blob.h>
#include <libhwsec-foundation/error/testing_helper.h>

#include "cryptohome/mock_platform.h"
#include "cryptohome/mock_tpm.h"

using ::hwsec::error::TPMError;
using ::hwsec::error::TPMRetryAction;
using ::hwsec_foundation::error::testing::ReturnError;
using ::testing::_;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::Return;

namespace cryptohome {

const base::FilePath kDefaultCryptohomeKeyFile(
    "/home/.shadow/cryptohome.ecc.key");
const TpmKeyHandle kTestKeyHandle = 17;  // any non-zero value

// Tests that need to do more setup work before calling Service::Initialize can
// use this instead of ServiceTest.
class CryptohomeEccKeyLoaderTest : public ::testing::Test {
 public:
  CryptohomeEccKeyLoaderTest() : cryptohome_key_loader_(&tpm_, &platform_) {}
  CryptohomeEccKeyLoaderTest(const CryptohomeEccKeyLoaderTest&) = delete;
  CryptohomeEccKeyLoaderTest& operator=(const CryptohomeEccKeyLoaderTest&) =
      delete;

  virtual ~CryptohomeEccKeyLoaderTest() = default;

  // Default mock implementations for |tpm_| methods.
  // For TPM-related flags: enabled is always true, other flags are settable.
  bool IsTpmOwned() const { return is_tpm_owned_; }
  void SetIsTpmOwned(bool is_tpm_owned) { is_tpm_owned_ = is_tpm_owned; }
  void SetUp() override {
    ON_CALL(tpm_, IsEnabled()).WillByDefault(Return(true));
    ON_CALL(tpm_, IsOwned())
        .WillByDefault(Invoke(this, &CryptohomeEccKeyLoaderTest::IsTpmOwned));
  }

  void TearDown() override {}

  Platform* GetPlatform() { return &platform_; }

 protected:
  bool HasStoredCryptohomeKey(std::string str) {
    std::string stored_key;
    if (!platform_.ReadFileToString(kDefaultCryptohomeKeyFile, &stored_key)) {
      return false;
    }
    if (stored_key != str) {
      return false;
    }
    return true;
  }

  bool HasLoadedCryptohomeKey(TpmKeyHandle handle) {
    if (!cryptohome_key_loader_.HasCryptohomeKey()) {
      return false;
    }
    TpmKeyHandle loaded_handle = cryptohome_key_loader_.GetCryptohomeKey();
    if (loaded_handle != handle) {
      return false;
    }
    return true;
  }

  bool is_tpm_owned_;
  std::map<base::FilePath, brillo::Blob> files_;
  NiceMock<MockTpm> tpm_;
  NiceMock<MockPlatform> platform_;

  // Declare cryptohome_key_loader_ last, so it gets destroyed before all the
  // mocks.
  CryptohomeEccKeyLoader cryptohome_key_loader_;
};

ACTION_P(GenerateWrappedKey, wrapped_key) {
  *arg0 = brillo::SecureBlob(wrapped_key);
  return true;
}

ACTION_P2(LoadWrappedKeyToHandle, tpm, handle) {
  arg1->reset(tpm, handle);
  return nullptr;
}

TEST_F(CryptohomeEccKeyLoaderTest, LoadCryptohomeKeySuccess) {
  platform_.WriteFile(kDefaultCryptohomeKeyFile, brillo::Blob());
  EXPECT_CALL(tpm_, LoadWrappedKey(_, _))
      .WillOnce(LoadWrappedKeyToHandle(&tpm_, kTestKeyHandle));
  cryptohome_key_loader_.Init();
  EXPECT_TRUE(HasLoadedCryptohomeKey(kTestKeyHandle));
  platform_.WriteFile(kDefaultCryptohomeKeyFile, brillo::Blob());
}

TEST_F(CryptohomeEccKeyLoaderTest, LoadCryptohomeKeyTransientFailure) {
  // Transient failure on the first attempt leads to key not being loaded.
  // But the key is not re-created. Success on the second attempt loads the
  // old key.
  platform_.WriteStringToFile(kDefaultCryptohomeKeyFile, "old-key");
  EXPECT_CALL(tpm_, LoadWrappedKey(_, _))
      .WillOnce(ReturnError<TPMError>("fake", TPMRetryAction::kCommunication))
      .WillOnce(LoadWrappedKeyToHandle(&tpm_, kTestKeyHandle));
  EXPECT_CALL(tpm_, CreateWrappedEccKey(_)).Times(0);
  cryptohome_key_loader_.Init();
  EXPECT_FALSE(cryptohome_key_loader_.HasCryptohomeKey());
  cryptohome_key_loader_.Init();
  EXPECT_TRUE(HasLoadedCryptohomeKey(kTestKeyHandle));
  EXPECT_TRUE(HasStoredCryptohomeKey("old-key"));
}

TEST_F(CryptohomeEccKeyLoaderTest, ReCreateCryptohomeKeyAfterLoadFailure) {
  // Permanent failure while loading the key leads to re-creating, storing
  // and loading the new key.
  SetIsTpmOwned(true);
  platform_.WriteStringToFile(kDefaultCryptohomeKeyFile, "old-key");
  EXPECT_CALL(tpm_, LoadWrappedKey(_, _))
      .WillOnce(ReturnError<TPMError>("fake", TPMRetryAction::kNoRetry))
      .WillOnce(LoadWrappedKeyToHandle(&tpm_, kTestKeyHandle));
  EXPECT_CALL(tpm_, CreateWrappedEccKey(_))
      .WillOnce(GenerateWrappedKey("new-key"));
  cryptohome_key_loader_.Init();
  EXPECT_TRUE(HasLoadedCryptohomeKey(kTestKeyHandle));
  EXPECT_TRUE(HasStoredCryptohomeKey("new-key"));
}

TEST_F(CryptohomeEccKeyLoaderTest,
       ReCreateCryptohomeKeyFailureDuringKeyCreation) {
  // Permanent failure while loading the key leads to an attempt to re-create
  // the key. Which fails. So, nothing new is stored or loaded.
  SetIsTpmOwned(true);
  platform_.WriteStringToFile(kDefaultCryptohomeKeyFile, "old-key");
  EXPECT_CALL(tpm_, LoadWrappedKey(_, _))
      .WillOnce(ReturnError<TPMError>("fake", TPMRetryAction::kNoRetry));
  EXPECT_CALL(tpm_, CreateWrappedEccKey(_)).WillOnce(Return(false));
  cryptohome_key_loader_.Init();
  EXPECT_FALSE(cryptohome_key_loader_.HasCryptohomeKey());
  EXPECT_TRUE(HasStoredCryptohomeKey("old-key"));
}

TEST_F(CryptohomeEccKeyLoaderTest,
       ReCreateCryptohomeKeyFailureDuringKeyLoading) {
  // Permanent failure while loading the key leads to re-creating the key.
  // It is stored. But then loading fails.
  // Still, on the next attempt, the key is loaded, and not re-created again.
  SetIsTpmOwned(true);
  platform_.WriteStringToFile(kDefaultCryptohomeKeyFile, "old-key");
  EXPECT_CALL(tpm_, LoadWrappedKey(_, _))
      .WillOnce(ReturnError<TPMError>("fake", TPMRetryAction::kNoRetry))
      .WillOnce(ReturnError<TPMError>("fake", TPMRetryAction::kNoRetry))
      .WillOnce(LoadWrappedKeyToHandle(&tpm_, kTestKeyHandle));
  EXPECT_CALL(tpm_, CreateWrappedEccKey(_))
      .WillOnce(GenerateWrappedKey("new-key"));
  cryptohome_key_loader_.Init();
  EXPECT_FALSE(cryptohome_key_loader_.HasCryptohomeKey());
  EXPECT_TRUE(HasStoredCryptohomeKey("new-key"));
  cryptohome_key_loader_.Init();
  EXPECT_TRUE(HasLoadedCryptohomeKey(kTestKeyHandle));
  EXPECT_TRUE(HasStoredCryptohomeKey("new-key"));
}

}  // namespace cryptohome
