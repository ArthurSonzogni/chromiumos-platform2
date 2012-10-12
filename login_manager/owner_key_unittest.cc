// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "login_manager/owner_key.h"

#include <vector>

#include <base/basictypes.h>
#include <base/file_path.h>
#include <base/file_util.h>
#include <base/logging.h>
#include <base/scoped_temp_dir.h>
#include <crypto/nss_util.h>
#include <crypto/rsa_private_key.h>
#include <gtest/gtest.h>

#include "login_manager/mock_nss_util.h"
#include "login_manager/nss_util.h"

namespace login_manager {

class CheckPublicKeyUtilFactory : public NssUtil::Factory {
 public:
  CheckPublicKeyUtilFactory(bool expected) : expected_(expected) {}
  virtual ~CheckPublicKeyUtilFactory() {}
  NssUtil* CreateNssUtil() {
    return new CheckPublicKeyUtil(expected_);
  }
 private:
  bool expected_;
  DISALLOW_COPY_AND_ASSIGN(CheckPublicKeyUtilFactory);
};

class OwnerKeyTest : public ::testing::Test {
 public:
  OwnerKeyTest() : factory_(true) {}
  virtual ~OwnerKeyTest() {}

  virtual void SetUp() {
    ASSERT_TRUE(tmpdir_.CreateUniqueTempDir());
    ASSERT_TRUE(file_util::CreateTemporaryFileInDir(tmpdir_.path(), &tmpfile_));
    ASSERT_EQ(2, file_util::WriteFile(tmpfile_, "a", 2));
    NssUtil::set_factory(&factory_);
  }

  virtual void TearDown() {
    NssUtil::set_factory(NULL);
  }

  void StartUnowned() {
    file_util::Delete(tmpfile_, false);
  }

  FilePath tmpfile_;

 private:
  CheckPublicKeyUtilFactory factory_;
  ScopedTempDir tmpdir_;
  DISALLOW_COPY_AND_ASSIGN(OwnerKeyTest);
};

TEST_F(OwnerKeyTest, Equals) {
  // Set up an empty key
  StartUnowned();
  OwnerKey key(tmpfile_);
  ASSERT_TRUE(key.PopulateFromDiskIfPossible());
  ASSERT_TRUE(key.HaveCheckedDisk());
  ASSERT_FALSE(key.IsPopulated());

  // Trivial case.
  EXPECT_TRUE(key.VEquals(std::vector<uint8>()));

  // Ensure that 0-length keys don't cause us to return true for everything.
  std::vector<uint8> fake(1, 1);
  EXPECT_FALSE(key.VEquals(fake));

  // Populate the key.
  ASSERT_TRUE(key.PopulateFromBuffer(fake));
  ASSERT_TRUE(key.HaveCheckedDisk());
  ASSERT_TRUE(key.IsPopulated());

  // Real comparison.
  EXPECT_TRUE(key.VEquals(fake));
}

TEST_F(OwnerKeyTest, LoadKey) {
  OwnerKey key(tmpfile_);
  ASSERT_FALSE(key.HaveCheckedDisk());
  ASSERT_FALSE(key.IsPopulated());
  ASSERT_TRUE(key.PopulateFromDiskIfPossible());
  ASSERT_TRUE(key.HaveCheckedDisk());
  ASSERT_TRUE(key.IsPopulated());
}

TEST_F(OwnerKeyTest, NoKeyToLoad) {
  StartUnowned();
  OwnerKey key(tmpfile_);
  ASSERT_FALSE(key.HaveCheckedDisk());
  ASSERT_FALSE(key.IsPopulated());
  ASSERT_TRUE(key.PopulateFromDiskIfPossible());
  ASSERT_TRUE(key.HaveCheckedDisk());
  ASSERT_FALSE(key.IsPopulated());
}

TEST_F(OwnerKeyTest, EmptyKeyToLoad) {
  ASSERT_EQ(0, file_util::WriteFile(tmpfile_, "", 0));
  ASSERT_TRUE(file_util::PathExists(tmpfile_));
  CheckPublicKeyUtilFactory factory(false);
  NssUtil::set_factory(&factory);

  OwnerKey key(tmpfile_);
  ASSERT_FALSE(key.HaveCheckedDisk());
  ASSERT_FALSE(key.IsPopulated());
  ASSERT_FALSE(key.PopulateFromDiskIfPossible());
  ASSERT_TRUE(key.HaveCheckedDisk());
  ASSERT_FALSE(key.IsPopulated());
}

TEST_F(OwnerKeyTest, NoKeyOnDiskAllowSetting) {
  StartUnowned();
  OwnerKey key(tmpfile_);
  ASSERT_FALSE(key.HaveCheckedDisk());
  ASSERT_FALSE(key.IsPopulated());
  ASSERT_TRUE(key.PopulateFromDiskIfPossible());
  ASSERT_TRUE(key.HaveCheckedDisk());
  ASSERT_FALSE(key.IsPopulated());

  std::vector<uint8> fake(1, 1);
  ASSERT_TRUE(key.PopulateFromBuffer(fake));
  ASSERT_TRUE(key.HaveCheckedDisk());
  ASSERT_TRUE(key.IsPopulated());
}

TEST_F(OwnerKeyTest, EnforceDiskCheckFirst) {
  std::vector<uint8> fake(1, 1);

  OwnerKey key(tmpfile_);
  ASSERT_FALSE(key.HaveCheckedDisk());
  ASSERT_FALSE(key.IsPopulated());
  ASSERT_FALSE(key.PopulateFromBuffer(fake));
  ASSERT_FALSE(key.IsPopulated());
  ASSERT_FALSE(key.HaveCheckedDisk());
}

TEST_F(OwnerKeyTest, RefuseToClobberInMemory) {
  std::vector<uint8> fake(1, 1);

  OwnerKey key(tmpfile_);
  ASSERT_FALSE(key.HaveCheckedDisk());
  ASSERT_FALSE(key.IsPopulated());

  ASSERT_TRUE(key.PopulateFromDiskIfPossible());
  ASSERT_TRUE(key.HaveCheckedDisk());
  ASSERT_TRUE(key.IsPopulated());

  ASSERT_FALSE(key.PopulateFromBuffer(fake));
  ASSERT_TRUE(key.HaveCheckedDisk());
  ASSERT_TRUE(key.IsPopulated());
}

TEST_F(OwnerKeyTest, RefuseToClobberOnDisk) {
  OwnerKey key(tmpfile_);
  ASSERT_FALSE(key.HaveCheckedDisk());
  ASSERT_FALSE(key.IsPopulated());

  ASSERT_TRUE(key.PopulateFromDiskIfPossible());
  ASSERT_TRUE(key.HaveCheckedDisk());
  ASSERT_TRUE(key.IsPopulated());

  ASSERT_FALSE(key.Persist());
  ASSERT_TRUE(key.HaveCheckedDisk());
  ASSERT_TRUE(key.IsPopulated());
}

TEST_F(OwnerKeyTest, SignVerify) {
  NssUtil::set_factory(NULL);  // Use real NSS.
  StartUnowned();
  OwnerKey key(tmpfile_);

  crypto::EnsureNSSInit();
  crypto::OpenPersistentNSSDB();
  scoped_ptr<crypto::RSAPrivateKey> pair(
      crypto::RSAPrivateKey::CreateSensitive(512));
  ASSERT_NE(pair.get(), reinterpret_cast<crypto::RSAPrivateKey*>(NULL));

  ASSERT_TRUE(key.PopulateFromDiskIfPossible());
  ASSERT_TRUE(key.HaveCheckedDisk());
  ASSERT_FALSE(key.IsPopulated());

  std::vector<uint8> to_export;
  ASSERT_TRUE(pair->ExportPublicKey(&to_export));
  ASSERT_TRUE(key.PopulateFromBuffer(to_export));
  ASSERT_TRUE(key.HaveCheckedDisk());
  ASSERT_TRUE(key.IsPopulated());

  std::string data("whatever");
  const uint8* data_p = reinterpret_cast<const uint8*>(data.c_str());
  std::vector<uint8> signature;
  EXPECT_TRUE(key.Sign(data_p, data.length(), &signature));
  EXPECT_TRUE(key.Verify(data_p,
                         data.length(),
                         &signature[0],
                         signature.size()));
}

TEST_F(OwnerKeyTest, RotateKey) {
  NssUtil::set_factory(NULL);  // Use real NSS.
  StartUnowned();
  OwnerKey key(tmpfile_);

  crypto::EnsureNSSInit();
  crypto::OpenPersistentNSSDB();
  scoped_ptr<crypto::RSAPrivateKey> pair(
      crypto::RSAPrivateKey::CreateSensitive(512));
  ASSERT_NE(pair.get(), reinterpret_cast<crypto::RSAPrivateKey*>(NULL));

  ASSERT_TRUE(key.PopulateFromDiskIfPossible());
  ASSERT_TRUE(key.HaveCheckedDisk());
  ASSERT_FALSE(key.IsPopulated());

  std::vector<uint8> to_export;
  ASSERT_TRUE(pair->ExportPublicKey(&to_export));
  ASSERT_TRUE(key.PopulateFromBuffer(to_export));
  ASSERT_TRUE(key.HaveCheckedDisk());
  ASSERT_TRUE(key.IsPopulated());
  ASSERT_TRUE(key.Persist());

  OwnerKey key2(tmpfile_);
  ASSERT_TRUE(key2.PopulateFromDiskIfPossible());
  ASSERT_TRUE(key2.HaveCheckedDisk());
  ASSERT_TRUE(key2.IsPopulated());

  scoped_ptr<crypto::RSAPrivateKey> new_pair(
      crypto::RSAPrivateKey::CreateSensitive(512));
  ASSERT_NE(new_pair.get(), reinterpret_cast<crypto::RSAPrivateKey*>(NULL));
  std::vector<uint8> new_export;
  ASSERT_TRUE(new_pair->ExportPublicKey(&new_export));

  std::vector<uint8> signature;
  ASSERT_TRUE(key2.Sign(&new_export[0], new_export.size(), &signature));
  ASSERT_TRUE(key2.Rotate(new_export, signature));
  ASSERT_TRUE(key2.Persist());
}

TEST_F(OwnerKeyTest, ClobberKey) {
  OwnerKey key(tmpfile_);

  ASSERT_TRUE(key.PopulateFromDiskIfPossible());
  ASSERT_TRUE(key.HaveCheckedDisk());
  ASSERT_TRUE(key.IsPopulated());

  std::vector<uint8> fake(1, 1);
  key.ClobberCompromisedKey(fake);
  ASSERT_TRUE(key.VEquals(fake));
  ASSERT_TRUE(key.Persist());
}

TEST_F(OwnerKeyTest, ResetKey) {
  OwnerKey key(tmpfile_);

  ASSERT_TRUE(key.PopulateFromDiskIfPossible());
  ASSERT_TRUE(key.HaveCheckedDisk());
  ASSERT_TRUE(key.IsPopulated());

  key.ClobberCompromisedKey(std::vector<uint8>());
  ASSERT_TRUE(!key.IsPopulated());
  ASSERT_TRUE(key.Persist());
  ASSERT_FALSE(file_util::PathExists(tmpfile_));
}

}  // namespace login_manager
