// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Unit tests for TpmPersistentState.

#include "cryptohome/tpm_persistent_state.h"

#include <map>

#include <base/files/file_path.h>
#include <brillo/secure_blob.h>

#include "cryptohome/mock_platform.h"

using brillo::SecureBlob;

using ::testing::_;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::Return;

namespace cryptohome {

class TpmPersistentStateTest : public ::testing::Test {
 public:
  // Default mock implementations for |platform_| methods.
  // Files are emulated using |files_| map: <file path> -> <file contents>.
  bool FileExists(const base::FilePath& path) const {
    return files_.count(path) > 0;
  }
  bool FileDelete(const base::FilePath& path) {
    return files_.erase(path) == 1;
  }
  bool FileTouch(const base::FilePath& path) {
    if (!FileExists(path)) {
      files_.emplace(path, brillo::Blob());
    }
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
  bool FileWrite(const base::FilePath& path, const brillo::Blob& blob) {
    files_[path] = blob;
    return true;
  }

  bool FileWriteSecureBlob(const base::FilePath& path,
                           const brillo::SecureBlob& sblob) {
    return FileWrite(path, brillo::Blob(sblob.begin(), sblob.end()));
  }

  bool FileWriteAtomic(const base::FilePath& path,
                       const brillo::SecureBlob& blob,
                       mode_t /* mode */) {
    return FileWriteSecureBlob(path, blob);
  }

  void SetUp() override {
    ON_CALL(platform_, FileExists(_))
        .WillByDefault(Invoke(this, &TpmPersistentStateTest::FileExists));
    ON_CALL(platform_, DeleteFileDurable(_))
        .WillByDefault(Invoke(this, &TpmPersistentStateTest::FileDelete));
    ON_CALL(platform_, TouchFileDurable(_))
        .WillByDefault(Invoke(this, &TpmPersistentStateTest::FileTouch));
    ON_CALL(platform_, GetFileSize(_, _))
        .WillByDefault(Invoke(this, &TpmPersistentStateTest::GetFileSize));
    ON_CALL(platform_, ReadFile(_, _))
        .WillByDefault(Invoke(this, &TpmPersistentStateTest::FileRead));
    ON_CALL(platform_, WriteSecureBlobToFile(_, _))
        .WillByDefault(
            Invoke(this, &TpmPersistentStateTest::FileWriteSecureBlob));
    ON_CALL(platform_, WriteSecureBlobToFileAtomicDurable(_, _, _))
        .WillByDefault(Invoke(this, &TpmPersistentStateTest::FileWriteAtomic));
    ON_CALL(platform_, DataSyncFile(_)).WillByDefault(Return(true));
  }

 protected:
  std::map<base::FilePath, brillo::Blob> files_;
  NiceMock<MockPlatform> platform_;

  // Declare tpm_init_ last, so it gets destroyed before all the mocks.
  TpmPersistentState tpm_persistent_state_{&platform_};
};

}  // namespace cryptohome
