// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/cryptohome_rsa_key_loader.h"

#include <map>
#include <string>

#include <base/files/file_path.h>
#include <brillo/secure_blob.h>
#include <libhwsec-foundation/error/testing_helper.h>

#include "cryptohome/mock_platform.h"
#include "cryptohome/mock_tpm.h"

using ::hwsec::error::TPMError;
using ::hwsec::error::TPMErrorBase;
using ::hwsec::error::TPMRetryAction;
using ::hwsec_foundation::error::testing::ReturnError;
using ::testing::_;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::Return;

namespace cryptohome {

const base::FilePath kDefaultCryptohomeKeyFile("/home/.shadow/cryptohome.key");
const TpmKeyHandle kTestKeyHandle = 17;  // any non-zero value

// Tests that need to do more setup work before calling Service::Initialize can
// use this instead of ServiceTest.
class CryptohomeRsaKeyLoaderTest : public ::testing::Test {
 public:
  CryptohomeRsaKeyLoaderTest() : cryptohome_key_loader_(&tpm_, &platform_) {}
  CryptohomeRsaKeyLoaderTest(const CryptohomeRsaKeyLoaderTest&) = delete;
  CryptohomeRsaKeyLoaderTest& operator=(const CryptohomeRsaKeyLoaderTest&) =
      delete;

  ~CryptohomeRsaKeyLoaderTest() override = default;

  // Default mock implementations for |tpm_| methods.
  // For TPM-related flags: enabled is always true, other flags are settable.
  bool IsTpmOwned() const { return is_tpm_owned_; }
  void SetIsTpmOwned(bool is_tpm_owned) { is_tpm_owned_ = is_tpm_owned; }
  TPMErrorBase GetRandomDataBlob(size_t length, brillo::Blob* data) const {
    data->resize(length, 0);
    return nullptr;
  }
  TPMErrorBase GetRandomDataSecureBlob(size_t length,
                                       brillo::SecureBlob* data) const {
    data->resize(length, 0);
    return nullptr;
  }

  // Default mock implementations for |platform_| methods.
  // Files are emulated using |files_| map: <file path> -> <file contents>.
  bool FileExists(const base::FilePath& path) const {
    return files_.count(path) > 0;
  }
  bool FileMove(const base::FilePath& from, const base::FilePath& to) {
    if (!FileExists(from)) {
      return false;
    }
    if (FileExists(to)) {
      return false;
    }
    files_[to] = files_[from];
    return FileDelete(from);
  }
  bool FileDelete(const base::FilePath& path) {
    return files_.erase(path) == 1;
  }
  bool FileTouch(const base::FilePath& path) {
    files_.emplace(path, brillo::Blob());
    return FileExists(path);
  }
  bool GetFileSize(const base::FilePath& path, int64_t* size) {
    if (!FileExists(path)) {
      return false;
    }
    *size = files_[path].size();
    return true;
  }
  bool FileRead(const base::FilePath& path, brillo::Blob* blob) {
    if (!FileExists(path)) {
      return false;
    }
    *blob = files_[path];
    return true;
  }
  bool FileReadToString(const base::FilePath& path, std::string* str) {
    brillo::Blob blob;
    if (!FileRead(path, &blob)) {
      return false;
    }
    str->assign(reinterpret_cast<char*>(blob.data()), blob.size());
    return true;
  }
  bool FileReadToSecureBlob(const base::FilePath& path,
                            brillo::SecureBlob* sblob) {
    if (!FileExists(path)) {
      return false;
    }
    brillo::Blob temp = files_[path];
    sblob->assign(temp.begin(), temp.end());
    return true;
  }
  bool FileWrite(const base::FilePath& path, const brillo::Blob& blob) {
    files_[path] = blob;
    return true;
  }
  bool FileWriteFromSecureBlob(const base::FilePath& path,
                               const brillo::SecureBlob& sblob) {
    brillo::Blob blob(sblob.begin(), sblob.end());
    files_[path] = blob;
    return true;
  }
  bool FileWriteAtomic(const base::FilePath& path,
                       const brillo::SecureBlob& sblob,
                       mode_t /* mode */) {
    return FileWriteFromSecureBlob(path, sblob);
  }
  bool FileWriteString(const base::FilePath& path, const std::string& str) {
    brillo::Blob blob(str.begin(), str.end());
    return FileWrite(path, blob);
  }

  void SetUp() override {
    ON_CALL(tpm_, IsEnabled()).WillByDefault(Return(true));
    ON_CALL(tpm_, IsOwned())
        .WillByDefault(Invoke(this, &CryptohomeRsaKeyLoaderTest::IsTpmOwned));

    ON_CALL(tpm_, GetRandomDataBlob(_, _))
        .WillByDefault(
            Invoke(this, &CryptohomeRsaKeyLoaderTest::GetRandomDataBlob));
    ON_CALL(tpm_, GetRandomDataSecureBlob(_, _))
        .WillByDefault(
            Invoke(this, &CryptohomeRsaKeyLoaderTest::GetRandomDataSecureBlob));

    ON_CALL(platform_, FileExists(_))
        .WillByDefault(Invoke(this, &CryptohomeRsaKeyLoaderTest::FileExists));
    ON_CALL(platform_, Move(_, _))
        .WillByDefault(Invoke(this, &CryptohomeRsaKeyLoaderTest::FileMove));
    ON_CALL(platform_, DeleteFile(_))
        .WillByDefault(Invoke(this, &CryptohomeRsaKeyLoaderTest::FileDelete));
    ON_CALL(platform_, DeletePathRecursively(_))
        .WillByDefault(Invoke(this, &CryptohomeRsaKeyLoaderTest::FileDelete));
    ON_CALL(platform_, DeleteFileDurable(_))
        .WillByDefault(Invoke(this, &CryptohomeRsaKeyLoaderTest::FileDelete));
    ON_CALL(platform_, TouchFileDurable(_))
        .WillByDefault(Invoke(this, &CryptohomeRsaKeyLoaderTest::FileTouch));
    ON_CALL(platform_, GetFileSize(_, _))
        .WillByDefault(Invoke(this, &CryptohomeRsaKeyLoaderTest::GetFileSize));
    ON_CALL(platform_, ReadFile(_, _))
        .WillByDefault(Invoke(this, &CryptohomeRsaKeyLoaderTest::FileRead));
    ON_CALL(platform_, ReadFileToSecureBlob(_, _))
        .WillByDefault(
            Invoke(this, &CryptohomeRsaKeyLoaderTest::FileReadToSecureBlob));
    ON_CALL(platform_, WriteSecureBlobToFile(_, _))
        .WillByDefault(
            Invoke(this, &CryptohomeRsaKeyLoaderTest::FileWriteFromSecureBlob));
    ON_CALL(platform_, WriteSecureBlobToFileAtomic(_, _, _))
        .WillByDefault(
            Invoke(this, &CryptohomeRsaKeyLoaderTest::FileWriteAtomic));
    ON_CALL(platform_, WriteSecureBlobToFileAtomicDurable(_, _, _))
        .WillByDefault(
            Invoke(this, &CryptohomeRsaKeyLoaderTest::FileWriteAtomic));
    ON_CALL(platform_, DataSyncFile(_)).WillByDefault(Return(true));
  }

  void TearDown() override {}

 protected:
  bool is_tpm_owned_ = false;
  std::map<base::FilePath, brillo::Blob> files_;
  NiceMock<MockTpm> tpm_;
  NiceMock<MockPlatform> platform_;

  // Declare cryptohome_key_loader_ last, so it gets destroyed before all the
  // mocks.
  CryptohomeRsaKeyLoader cryptohome_key_loader_;
};

MATCHER_P(HasStoredCryptohomeKey, str, "") {
  std::string stored_key;
  if (!arg->FileReadToString(kDefaultCryptohomeKeyFile, &stored_key)) {
    *result_listener << "has no stored cryptohome key";
    return false;
  }
  if (stored_key != str) {
    *result_listener << "has stored cryptohome key \"" << stored_key << "\"";
    return false;
  }
  return true;
}

MATCHER_P(HasLoadedCryptohomeKey, handle, "") {
  if (!arg->HasCryptohomeKey()) {
    *result_listener << "has no loaded cryptohome key";
    return false;
  }
  TpmKeyHandle loaded_handle = arg->GetCryptohomeKey();
  if (loaded_handle != handle) {
    *result_listener << "has loaded cryptohome key " << loaded_handle;
    return false;
  }
  return true;
}

MATCHER(HasNoLoadedCryptohomeKey, "") {
  if (arg->HasCryptohomeKey()) {
    TpmKeyHandle loaded_handle = arg->GetCryptohomeKey();
    *result_listener << "has loaded cryptohome key " << loaded_handle;
    return false;
  }
  return true;
}

ACTION_P(GenerateWrappedKey, wrapped_key) {
  *arg2 = brillo::SecureBlob(wrapped_key);
  return true;
}

ACTION_P2(LoadWrappedKeyToHandle, tpm, handle) {
  arg1->reset(tpm, handle);
  return nullptr;
}

TEST_F(CryptohomeRsaKeyLoaderTest, LoadCryptohomeKeySuccess) {
  SetIsTpmOwned(true);
  FileTouch(kDefaultCryptohomeKeyFile);
  EXPECT_CALL(tpm_, LoadWrappedKey(_, _))
      .WillOnce(LoadWrappedKeyToHandle(&tpm_, kTestKeyHandle));
  cryptohome_key_loader_.Init();
  EXPECT_THAT(&cryptohome_key_loader_, HasLoadedCryptohomeKey(kTestKeyHandle));
}

TEST_F(CryptohomeRsaKeyLoaderTest, LoadCryptohomeKeyNotOwned) {
  SetIsTpmOwned(false);
  FileTouch(kDefaultCryptohomeKeyFile);
  EXPECT_CALL(tpm_, LoadWrappedKey(_, _)).Times(0);
  cryptohome_key_loader_.Init();
  EXPECT_THAT(&cryptohome_key_loader_, HasNoLoadedCryptohomeKey());
}

TEST_F(CryptohomeRsaKeyLoaderTest, LoadCryptohomeKeyTransientFailure) {
  // Transient failure on the first attempt leads to key not being loaded.
  // But the key is not re-created. Success on the second attempt loads the
  // old key.
  SetIsTpmOwned(true);
  FileWriteString(kDefaultCryptohomeKeyFile, "old-key");
  EXPECT_CALL(tpm_, LoadWrappedKey(_, _))
      .WillOnce(ReturnError<TPMError>("fake", TPMRetryAction::kCommunication))
      .WillOnce(LoadWrappedKeyToHandle(&tpm_, kTestKeyHandle));
  EXPECT_CALL(tpm_, WrapRsaKey(_, _, _)).Times(0);
  cryptohome_key_loader_.Init();
  EXPECT_THAT(&cryptohome_key_loader_, HasNoLoadedCryptohomeKey());
  cryptohome_key_loader_.Init();
  EXPECT_THAT(&cryptohome_key_loader_, HasLoadedCryptohomeKey(kTestKeyHandle));
  EXPECT_THAT(this, HasStoredCryptohomeKey("old-key"));
}

TEST_F(CryptohomeRsaKeyLoaderTest, ReCreateCryptohomeKeyAfterLoadFailure) {
  // Permanent failure while loading the key leads to re-creating, storing
  // and loading the new key.
  SetIsTpmOwned(true);
  FileWriteString(kDefaultCryptohomeKeyFile, "old-key");
  EXPECT_CALL(tpm_, LoadWrappedKey(_, _))
      .WillOnce(ReturnError<TPMError>("fake", TPMRetryAction::kNoRetry))
      .WillOnce(LoadWrappedKeyToHandle(&tpm_, kTestKeyHandle));
  EXPECT_CALL(tpm_, WrapRsaKey(_, _, _))
      .WillOnce(GenerateWrappedKey("new-key"));
  cryptohome_key_loader_.Init();
  EXPECT_THAT(&cryptohome_key_loader_, HasLoadedCryptohomeKey(kTestKeyHandle));
  EXPECT_THAT(this, HasStoredCryptohomeKey("new-key"));
}

TEST_F(CryptohomeRsaKeyLoaderTest,
       ReCreateCryptohomeKeyFailureDuringKeyCreation) {
  // Permanent failure while loading the key leads to an attempt to re-create
  // the key. Which fails. So, nothing new is stored or loaded.
  SetIsTpmOwned(true);
  FileWriteString(kDefaultCryptohomeKeyFile, "old-key");
  EXPECT_CALL(tpm_, LoadWrappedKey(_, _))
      .WillOnce(ReturnError<TPMError>("fake", TPMRetryAction::kNoRetry));
  EXPECT_CALL(tpm_, WrapRsaKey(_, _, _)).WillOnce(Return(false));
  cryptohome_key_loader_.Init();
  EXPECT_THAT(&cryptohome_key_loader_, HasNoLoadedCryptohomeKey());
  EXPECT_THAT(this, HasStoredCryptohomeKey("old-key"));
}

TEST_F(CryptohomeRsaKeyLoaderTest,
       ReCreateCryptohomeKeyFailureDuringKeyLoading) {
  // Permanent failure while loading the key leads to re-creating the key.
  // It is stored. But then loading fails.
  // Still, on the next attempt, the key is loaded, and not re-created again.
  SetIsTpmOwned(true);
  FileWriteString(kDefaultCryptohomeKeyFile, "old-key");
  EXPECT_CALL(tpm_, LoadWrappedKey(_, _))
      .WillOnce(ReturnError<TPMError>("fake", TPMRetryAction::kNoRetry))
      .WillOnce(ReturnError<TPMError>("fake", TPMRetryAction::kNoRetry))
      .WillOnce(LoadWrappedKeyToHandle(&tpm_, kTestKeyHandle));
  EXPECT_CALL(tpm_, WrapRsaKey(_, _, _))
      .WillOnce(GenerateWrappedKey("new-key"));
  cryptohome_key_loader_.Init();
  EXPECT_THAT(&cryptohome_key_loader_, HasNoLoadedCryptohomeKey());
  EXPECT_THAT(this, HasStoredCryptohomeKey("new-key"));
  cryptohome_key_loader_.Init();
  EXPECT_THAT(&cryptohome_key_loader_, HasLoadedCryptohomeKey(kTestKeyHandle));
  EXPECT_THAT(this, HasStoredCryptohomeKey("new-key"));
}

}  // namespace cryptohome
