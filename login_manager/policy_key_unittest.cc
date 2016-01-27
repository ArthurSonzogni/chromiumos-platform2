// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "login_manager/policy_key.h"

#include <stdint.h>

#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/logging.h>
#include <crypto/nss_key_util.h>
#include <crypto/nss_util.h>
#include <crypto/nss_util_internal.h>
#include <crypto/rsa_private_key.h>
#include <gtest/gtest.h>

#include "login_manager/mock_nss_util.h"
#include "login_manager/nss_util.h"

namespace login_manager {

class PolicyKeyTest : public ::testing::Test {
 public:
  PolicyKeyTest() {}
  virtual ~PolicyKeyTest() {}

  virtual void SetUp() {
    ASSERT_TRUE(tmpdir_.CreateUniqueTempDir());
    ASSERT_TRUE(base::CreateTemporaryFileInDir(tmpdir_.path(), &tmpfile_));
    ASSERT_EQ(2, base::WriteFile(tmpfile_, "a", 2));
  }

  virtual void TearDown() {}

  void StartUnowned() { base::DeleteFile(tmpfile_, false); }

  static scoped_ptr<crypto::RSAPrivateKey> CreateRSAPrivateKey(
      PK11SlotInfo* slot,
      uint16_t num_bits) {
    scoped_ptr<crypto::RSAPrivateKey> key;
    crypto::ScopedSECKEYPublicKey public_key_obj;
    crypto::ScopedSECKEYPrivateKey private_key_obj;
    if (crypto::GenerateRSAKeyPairNSS(slot, num_bits, true /* permanent */,
                                      &public_key_obj, &private_key_obj)) {
      key.reset(crypto::RSAPrivateKey::CreateFromKey(private_key_obj.get()));
    }
    return key;
  }

  base::FilePath tmpfile_;

 private:
  base::ScopedTempDir tmpdir_;
  DISALLOW_COPY_AND_ASSIGN(PolicyKeyTest);
};

TEST_F(PolicyKeyTest, Equals) {
  // Set up an empty key
  StartUnowned();
  MockNssUtil noop_util;
  PolicyKey key(tmpfile_, &noop_util);
  ASSERT_TRUE(key.PopulateFromDiskIfPossible());
  ASSERT_TRUE(key.HaveCheckedDisk());
  ASSERT_FALSE(key.IsPopulated());

  // Trivial case.
  EXPECT_TRUE(key.VEquals(std::vector<uint8_t>()));

  // Ensure that 0-length keys don't cause us to return true for everything.
  std::vector<uint8_t> fake(1, 1);
  EXPECT_FALSE(key.VEquals(fake));

  // Populate the key.
  ASSERT_TRUE(key.PopulateFromBuffer(fake));
  ASSERT_TRUE(key.HaveCheckedDisk());
  ASSERT_TRUE(key.IsPopulated());

  // Real comparison.
  EXPECT_TRUE(key.VEquals(fake));
}

TEST_F(PolicyKeyTest, LoadKey) {
  CheckPublicKeyUtil good_key_util(true);
  PolicyKey key(tmpfile_, &good_key_util);
  ASSERT_FALSE(key.HaveCheckedDisk());
  ASSERT_FALSE(key.IsPopulated());
  ASSERT_TRUE(key.PopulateFromDiskIfPossible());
  ASSERT_TRUE(key.HaveCheckedDisk());
  ASSERT_TRUE(key.IsPopulated());
}

TEST_F(PolicyKeyTest, NoKeyToLoad) {
  StartUnowned();
  MockNssUtil noop_util;
  PolicyKey key(tmpfile_, &noop_util);
  ASSERT_FALSE(key.HaveCheckedDisk());
  ASSERT_FALSE(key.IsPopulated());
  ASSERT_TRUE(key.PopulateFromDiskIfPossible());
  ASSERT_TRUE(key.HaveCheckedDisk());
  ASSERT_FALSE(key.IsPopulated());
}

TEST_F(PolicyKeyTest, EmptyKeyToLoad) {
  ASSERT_EQ(0, base::WriteFile(tmpfile_, "", 0));
  ASSERT_TRUE(base::PathExists(tmpfile_));
  CheckPublicKeyUtil bad_key_util(false);

  PolicyKey key(tmpfile_, &bad_key_util);
  ASSERT_FALSE(key.HaveCheckedDisk());
  ASSERT_FALSE(key.IsPopulated());
  ASSERT_FALSE(key.PopulateFromDiskIfPossible());
  ASSERT_TRUE(key.HaveCheckedDisk());
  ASSERT_FALSE(key.IsPopulated());
}

TEST_F(PolicyKeyTest, NoKeyOnDiskAllowSetting) {
  StartUnowned();
  MockNssUtil noop_util;
  PolicyKey key(tmpfile_, &noop_util);
  ASSERT_FALSE(key.HaveCheckedDisk());
  ASSERT_FALSE(key.IsPopulated());
  ASSERT_TRUE(key.PopulateFromDiskIfPossible());
  ASSERT_TRUE(key.HaveCheckedDisk());
  ASSERT_FALSE(key.IsPopulated());

  std::vector<uint8_t> fake(1, 1);
  ASSERT_TRUE(key.PopulateFromBuffer(fake));
  ASSERT_TRUE(key.HaveCheckedDisk());
  ASSERT_TRUE(key.IsPopulated());
}

TEST_F(PolicyKeyTest, EnforceDiskCheckFirst) {
  std::vector<uint8_t> fake(1, 1);

  MockNssUtil noop_util;
  PolicyKey key(tmpfile_, &noop_util);
  ASSERT_FALSE(key.HaveCheckedDisk());
  ASSERT_FALSE(key.IsPopulated());
  ASSERT_FALSE(key.PopulateFromBuffer(fake));
  ASSERT_FALSE(key.IsPopulated());
  ASSERT_FALSE(key.HaveCheckedDisk());
}

TEST_F(PolicyKeyTest, RefuseToClobberInMemory) {
  std::vector<uint8_t> fake(1, 1);

  CheckPublicKeyUtil good_key_util(true);
  PolicyKey key(tmpfile_, &good_key_util);
  ASSERT_FALSE(key.HaveCheckedDisk());
  ASSERT_FALSE(key.IsPopulated());

  ASSERT_TRUE(key.PopulateFromDiskIfPossible());
  ASSERT_TRUE(key.HaveCheckedDisk());
  ASSERT_TRUE(key.IsPopulated());

  ASSERT_FALSE(key.PopulateFromBuffer(fake));
  ASSERT_TRUE(key.HaveCheckedDisk());
  ASSERT_TRUE(key.IsPopulated());
}

TEST_F(PolicyKeyTest, RefuseToClobberOnDisk) {
  CheckPublicKeyUtil good_key_util(true);
  PolicyKey key(tmpfile_, &good_key_util);
  ASSERT_FALSE(key.HaveCheckedDisk());
  ASSERT_FALSE(key.IsPopulated());

  ASSERT_TRUE(key.PopulateFromDiskIfPossible());
  ASSERT_TRUE(key.HaveCheckedDisk());
  ASSERT_TRUE(key.IsPopulated());

  ASSERT_FALSE(key.Persist());
  ASSERT_TRUE(key.HaveCheckedDisk());
  ASSERT_TRUE(key.IsPopulated());
}

TEST_F(PolicyKeyTest, SignVerify) {
  scoped_ptr<NssUtil> nss(NssUtil::Create());
  StartUnowned();
  PolicyKey key(tmpfile_, nss.get());
  crypto::ScopedTestNSSDB test_db;

  scoped_ptr<crypto::RSAPrivateKey> pair(
      CreateRSAPrivateKey(test_db.slot(), 512));
  ASSERT_NE(pair.get(), reinterpret_cast<crypto::RSAPrivateKey*>(NULL));

  ASSERT_TRUE(key.PopulateFromDiskIfPossible());
  ASSERT_TRUE(key.HaveCheckedDisk());
  ASSERT_FALSE(key.IsPopulated());

  std::vector<uint8_t> to_export;
  ASSERT_TRUE(pair->ExportPublicKey(&to_export));
  ASSERT_TRUE(key.PopulateFromBuffer(to_export));
  ASSERT_TRUE(key.HaveCheckedDisk());
  ASSERT_TRUE(key.IsPopulated());

  std::string data("whatever");
  const uint8_t* data_p = reinterpret_cast<const uint8_t*>(data.c_str());
  std::vector<uint8_t> signature;
  EXPECT_TRUE(nss->Sign(data_p, data.length(), &signature, pair.get()));
  EXPECT_TRUE(
      key.Verify(data_p, data.length(), &signature[0], signature.size()));
}

TEST_F(PolicyKeyTest, RotateKey) {
  scoped_ptr<NssUtil> nss(NssUtil::Create());
  StartUnowned();
  PolicyKey key(tmpfile_, nss.get());
  crypto::ScopedTestNSSDB test_db;

  scoped_ptr<crypto::RSAPrivateKey> pair(
      CreateRSAPrivateKey(test_db.slot(), 512));
  ASSERT_NE(pair.get(), reinterpret_cast<crypto::RSAPrivateKey*>(NULL));

  ASSERT_TRUE(key.PopulateFromDiskIfPossible());
  ASSERT_TRUE(key.HaveCheckedDisk());
  ASSERT_FALSE(key.IsPopulated());

  std::vector<uint8_t> to_export;
  ASSERT_TRUE(pair->ExportPublicKey(&to_export));
  ASSERT_TRUE(key.PopulateFromBuffer(to_export));
  ASSERT_TRUE(key.HaveCheckedDisk());
  ASSERT_TRUE(key.IsPopulated());
  ASSERT_TRUE(key.Persist());

  PolicyKey key2(tmpfile_, nss.get());
  ASSERT_TRUE(key2.PopulateFromDiskIfPossible());
  ASSERT_TRUE(key2.HaveCheckedDisk());
  ASSERT_TRUE(key2.IsPopulated());

  scoped_ptr<crypto::RSAPrivateKey> new_pair(
      CreateRSAPrivateKey(test_db.slot(), 512));
  ASSERT_NE(new_pair.get(), reinterpret_cast<crypto::RSAPrivateKey*>(NULL));
  std::vector<uint8_t> new_export;
  ASSERT_TRUE(new_pair->ExportPublicKey(&new_export));

  std::vector<uint8_t> signature;
  ASSERT_TRUE(
      nss->Sign(&new_export[0], new_export.size(), &signature, pair.get()));
  ASSERT_TRUE(key2.Rotate(new_export, signature));
  ASSERT_TRUE(key2.Persist());
}

TEST_F(PolicyKeyTest, ClobberKey) {
  CheckPublicKeyUtil good_key_util(true);
  PolicyKey key(tmpfile_, &good_key_util);

  ASSERT_TRUE(key.PopulateFromDiskIfPossible());
  ASSERT_TRUE(key.HaveCheckedDisk());
  ASSERT_TRUE(key.IsPopulated());

  std::vector<uint8_t> fake(1, 1);
  key.ClobberCompromisedKey(fake);
  ASSERT_TRUE(key.VEquals(fake));
  ASSERT_TRUE(key.Persist());
}

TEST_F(PolicyKeyTest, ResetKey) {
  CheckPublicKeyUtil good_key_util(true);
  PolicyKey key(tmpfile_, &good_key_util);

  ASSERT_TRUE(key.PopulateFromDiskIfPossible());
  ASSERT_TRUE(key.HaveCheckedDisk());
  ASSERT_TRUE(key.IsPopulated());

  key.ClobberCompromisedKey(std::vector<uint8_t>());
  ASSERT_TRUE(!key.IsPopulated());
  ASSERT_TRUE(key.Persist());
  ASSERT_FALSE(base::PathExists(tmpfile_));
}

}  // namespace login_manager
