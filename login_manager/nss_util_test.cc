// Copyright 2012 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "login_manager/nss_util.h"

#include <stdint.h>

#include <memory>
#include <optional>

#include <base/base64.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <crypto/nss_util.h>
#include <crypto/rsa_private_key.h>
#include <crypto/scoped_nss_types.h>
#include <gtest/gtest.h>

using crypto::ScopedPK11Slot;

namespace login_manager {
class NssUtilTest : public ::testing::Test {
 public:
  NssUtilTest() : util_(NssUtil::Create()) {}
  NssUtilTest(const NssUtilTest&) = delete;
  NssUtilTest& operator=(const NssUtilTest&) = delete;

  ~NssUtilTest() override {}

 protected:
  static const char kUsername[];
  base::ScopedTempDir tmpdir_;
  std::unique_ptr<NssUtil> util_;
  ScopedPK11SlotDescriptor desc_;
};

const char NssUtilTest::kUsername[] = "someone@nowhere.com";

TEST_F(NssUtilTest, AcceptGoodPublicKey) {
  std::vector<uint8_t> public_key =
      base::Base64Decode(
          "MFwwDQYJKoZIhvcNAQEBBQADSwAwSAJBAMMQTKX6mem9D7UomHUs54dWeASj9s3VaJ3K"
          "tJa+BId9AYIjJn4cY4N/aW7Wkm7MyHvapawgh8QTxP0Hekzb2hkCAwEAAQ==")
          .value();
  EXPECT_TRUE(util_->CheckPublicKeyBlob(public_key));
}

TEST_F(NssUtilTest, RejectBadPublicKey) {
  std::vector<uint8_t> public_key(10, 'a');
  EXPECT_FALSE(util_->CheckPublicKeyBlob(public_key));
}

}  // namespace login_manager
