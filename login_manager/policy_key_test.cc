// Copyright 2012 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "login_manager/policy_key.h"

#include <stdint.h>

#include <memory>
#include <string>
#include <vector>

#include <base/base64.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/logging.h>
#include <brillo/files/file_util.h>
#include <crypto/nss_key_util.h>
#include <crypto/nss_util.h>
#include <crypto/nss_util_internal.h>
#include <crypto/rsa_private_key.h>
#include <gtest/gtest.h>

#include "login_manager/blob_util.h"
#include "login_manager/mock_nss_util.h"
#include "login_manager/nss_util.h"

namespace login_manager {

class PolicyKeyTest : public ::testing::Test {
 public:
  PolicyKeyTest() {}
  PolicyKeyTest(const PolicyKeyTest&) = delete;
  PolicyKeyTest& operator=(const PolicyKeyTest&) = delete;

  ~PolicyKeyTest() override {}

  void SetUp() override {
    ASSERT_TRUE(tmpdir_.CreateUniqueTempDir());
    ASSERT_TRUE(base::CreateTemporaryFileInDir(tmpdir_.GetPath(), &tmpfile_));
    ASSERT_EQ(2, base::WriteFile(tmpfile_, "a", 2));
  }

  void TearDown() override {}

  void StartUnowned() { brillo::DeleteFile(tmpfile_); }

  static std::unique_ptr<crypto::RSAPrivateKey> CreateRSAPrivateKey(
      PK11SlotInfo* slot, uint16_t num_bits) {
    std::unique_ptr<crypto::RSAPrivateKey> key;
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
  const std::vector<uint8_t> fake = {1};
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

  const std::vector<uint8_t> fake = {1};
  ASSERT_TRUE(key.PopulateFromBuffer(fake));
  ASSERT_TRUE(key.HaveCheckedDisk());
  ASSERT_TRUE(key.IsPopulated());
}

TEST_F(PolicyKeyTest, EnforceDiskCheckFirst) {
  const std::vector<uint8_t> fake = {1};

  MockNssUtil noop_util;
  PolicyKey key(tmpfile_, &noop_util);
  ASSERT_FALSE(key.HaveCheckedDisk());
  ASSERT_FALSE(key.IsPopulated());
  ASSERT_FALSE(key.PopulateFromBuffer(fake));
  ASSERT_FALSE(key.IsPopulated());
  ASSERT_FALSE(key.HaveCheckedDisk());
}

TEST_F(PolicyKeyTest, RefuseToClobberInMemory) {
  const std::vector<uint8_t> fake = {1};

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

TEST_F(PolicyKeyTest, Verify) {
  std::unique_ptr<NssUtil> nss(NssUtil::Create());
  StartUnowned();
  PolicyKey key(tmpfile_, nss.get());
  crypto::ScopedTestNSSDB test_db;

  ASSERT_TRUE(key.PopulateFromDiskIfPossible());
  ASSERT_TRUE(key.HaveCheckedDisk());
  ASSERT_FALSE(key.IsPopulated());

  std::vector<uint8_t> key_spki =
      base::Base64Decode(
          "MFwwDQYJKoZIhvcNAQEBBQADSwAwSAJBAMJQ/WDsfE3NYLUSkN6T9Ls3q0S/"
          "ZWK1nI5MFvgAPtnSi0OmXvtLe385y4rs6PvxX8DAPqLomHlOr4N8qChCApMCAwEAAQ="
          "=")
          .value();
  ASSERT_TRUE(key.PopulateFromBuffer(key_spki));

  ASSERT_TRUE(key.HaveCheckedDisk());
  ASSERT_TRUE(key.IsPopulated());

  const std::vector<uint8_t> data = StringToBlob("whatever");
  // The signature for `data` generated by the key from `key_spki`.
  std::vector<uint8_t> signature =
      base::Base64Decode(
          "c7k3PeObiUGNze7Fi7cU97uUBmZ4NorcFxUFggwFcYtJUnxn2xEIUCdEAXJwJqK/"
          "cdnzu+fWViU2XBEUIfi60w==")
          .value();

  EXPECT_TRUE(
      key.Verify(data, signature, crypto::SignatureVerifier::RSA_PKCS1_SHA1));
}

TEST_F(PolicyKeyTest, RotateKey) {
  std::unique_ptr<NssUtil> nss(NssUtil::Create());
  StartUnowned();
  PolicyKey key(tmpfile_, nss.get());
  crypto::ScopedTestNSSDB test_db;

  ASSERT_TRUE(key.PopulateFromDiskIfPossible());
  ASSERT_TRUE(key.HaveCheckedDisk());
  ASSERT_FALSE(key.IsPopulated());

  std::vector<uint8_t> key_1_spki =
      base::Base64Decode(
          "MFwwDQYJKoZIhvcNAQEBBQADSwAwSAJBAMMQTKX6mem9D7UomHUs54dWeASj9s3VaJ3K"
          "tJa+BId9AYIjJn4cY4N/aW7Wkm7MyHvapawgh8QTxP0Hekzb2hkCAwEAAQ==")
          .value();
  ASSERT_TRUE(key.PopulateFromBuffer(key_1_spki));

  ASSERT_TRUE(key.HaveCheckedDisk());
  ASSERT_TRUE(key.IsPopulated());
  ASSERT_TRUE(key.Persist());

  PolicyKey key2(tmpfile_, nss.get());
  ASSERT_TRUE(key2.PopulateFromDiskIfPossible());
  ASSERT_TRUE(key2.HaveCheckedDisk());
  ASSERT_TRUE(key2.IsPopulated());

  std::vector<uint8_t> key_2_spki =
      base::Base64Decode(
          "MFwwDQYJKoZIhvcNAQEBBQADSwAwSAJBALdXiSVm7mqq0cqJ6AZ2mYRFAIFlqEVpdelR"
          "EtzGRXLul7nVmw20cr/lk02h9CiSUvVupQO23Kcfa3XVBS/nXccCAwEAAQ==")
          .value();
  // The signature of `key_2_spki` generated by the key from `key_1_spki`.
  std::vector<uint8_t> key_2_signature =
      base::Base64Decode(
          "kC2NAP7sQw7P5RAvHPIrHK9FuGR9PWuAkF64INLwXjPATYadbZiKBLmT/zUjKtSpsvK/"
          "oJvHCxMPTpVK153DTw==")
          .value();

  ASSERT_TRUE(key2.Rotate(key_2_spki, key_2_signature,
                          crypto::SignatureVerifier::RSA_PKCS1_SHA1));
  ASSERT_TRUE(key2.Persist());
}

TEST_F(PolicyKeyTest, ClobberKey) {
  CheckPublicKeyUtil good_key_util(true);
  PolicyKey key(tmpfile_, &good_key_util);

  ASSERT_TRUE(key.PopulateFromDiskIfPossible());
  ASSERT_TRUE(key.HaveCheckedDisk());
  ASSERT_TRUE(key.IsPopulated());

  const std::vector<uint8_t> fake = {1};
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
