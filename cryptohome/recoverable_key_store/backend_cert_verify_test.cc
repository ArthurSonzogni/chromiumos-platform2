// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/recoverable_key_store/backend_cert_verify.h"

#include <optional>
#include <string>
#include <vector>

#include <base/base64.h>
#include <brillo/secure_blob.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <metrics/metrics_library_mock.h>

#include "cryptohome/cryptohome_metrics.h"
#include "cryptohome/recoverable_key_store/backend_cert_test_constants.h"

namespace cryptohome {

namespace {

using ::testing::Each;
using ::testing::Field;
using ::testing::Optional;
using ::testing::SizeIs;
using ::testing::StrictMock;

constexpr char kVerifyAndParseBackendCertResult[] =
    "Cryptohome.RecoverableKeyStore.VerifyAndParseCertResult";

class RecoverableKeyStoreBackendCertVerifyTest : public ::testing::Test {
 public:
  RecoverableKeyStoreBackendCertVerifyTest() {
    OverrideMetricsLibraryForTesting(&metrics_);
  }
  ~RecoverableKeyStoreBackendCertVerifyTest() override {
    ClearMetricsLibraryForTesting();
  }

 protected:
  void ExpectSendVerifyAndParseBackendCertResult(
      VerifyAndParseBackendCertResult result) {
    EXPECT_CALL(
        metrics_,
        SendEnumToUMA(
            kVerifyAndParseBackendCertResult, static_cast<int>(result),
            static_cast<int>(VerifyAndParseBackendCertResult::kMaxValue) + 1));
  }

 private:
  StrictMock<MetricsLibraryMock> metrics_;
};

TEST_F(RecoverableKeyStoreBackendCertVerifyTest, GetCertXmlVersion) {
  std::string cert_xml;
  ASSERT_TRUE(base::Base64Decode(kCertXml10014B64, &cert_xml));
  EXPECT_THAT(GetCertXmlVersion(cert_xml), Optional(10014));
}

TEST_F(RecoverableKeyStoreBackendCertVerifyTest, GetCertXmlVersionFailed) {
  const std::string cert_xml = "not a xml";
  EXPECT_EQ(GetCertXmlVersion(cert_xml), std::nullopt);
}

// Ideally we would also include tests of the certificate verification process.
// Unfortunately, these tests can't be done in a hermetic way: certs have
// expiration dates and we can't easily control the timestamp that the
// underlying libraries check during verification and so any such unit test
// would be time-dependent and so would break once the cert expired.

}  // namespace
}  // namespace cryptohome
