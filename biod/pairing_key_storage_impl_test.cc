// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "biod/pairing_key_storage_impl.h"

#include <memory>
#include <optional>
#include <string>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/important_file_writer.h>
#include <base/files/scoped_temp_dir.h>
#include <brillo/files/file_util.h>
#include <brillo/scoped_umask.h>
#include <brillo/secure_blob.h>
#include <gtest/gtest.h>

namespace biod {

namespace {
constexpr char kAuthStackManagerName[] = "manager";
}  // namespace

class PairingKeyStorageTest : public ::testing::Test {
 public:
  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    root_path_ = temp_dir_.GetPath().AppendASCII("pk_stoage_test_root");
    pk_storage_ = std::make_unique<PairingKeyStorageImpl>(
        root_path_.value(), kAuthStackManagerName);
  }

 protected:
  base::ScopedTempDir temp_dir_;
  base::FilePath root_path_;
  std::unique_ptr<PairingKeyStorageImpl> pk_storage_;
};

TEST_F(PairingKeyStorageTest, ReadWriteSuccess) {
  const brillo::Blob kWrappedPk(10, 1);

  // At first, there should be no Pk.
  EXPECT_FALSE(pk_storage_->PairingKeyExists());

  // After writing Pk, PairingKeyExists should be true.
  EXPECT_TRUE(pk_storage_->WriteWrappedPairingKey(kWrappedPk));
  EXPECT_TRUE(pk_storage_->PairingKeyExists());

  // ReadWrappedPairingKey should return the data we just wrote.
  std::optional<brillo::Blob> wrapped_pk = pk_storage_->ReadWrappedPairingKey();
  ASSERT_TRUE(wrapped_pk.has_value());
  EXPECT_EQ(wrapped_pk, kWrappedPk);
}

}  // namespace biod
