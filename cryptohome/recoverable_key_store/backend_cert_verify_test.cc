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

TEST_F(RecoverableKeyStoreBackendCertVerifyTest, Success) {
  std::string cert_xml, sig_xml;
  ASSERT_TRUE(base::Base64Decode(kCertXml10014B64, &cert_xml));
  ASSERT_TRUE(base::Base64Decode(kSigXml10014B64, &sig_xml));

  ExpectSendVerifyAndParseBackendCertResult(
      VerifyAndParseBackendCertResult::kSuccess);
  std::optional<RecoverableKeyStoreCertList> cert_list =
      VerifyAndParseRecoverableKeyStoreBackendCertXmls(cert_xml, sig_xml);
  ASSERT_TRUE(cert_list.has_value());
  EXPECT_EQ(cert_list->version, 10014);
  // SecureBox-encoded public key size is 65.
  EXPECT_THAT(cert_list->certs,
              Each(Field(&RecoverableKeyStoreCert::public_key, SizeIs(65))));
}

TEST_F(RecoverableKeyStoreBackendCertVerifyTest, FailNotXml) {
  const std::string sig_xml = "not a xml";
  std::string cert_xml;
  ASSERT_TRUE(base::Base64Decode(kCertXml10014B64, &cert_xml));

  ExpectSendVerifyAndParseBackendCertResult(
      VerifyAndParseBackendCertResult::kParseSignatureFailed);
  EXPECT_EQ(VerifyAndParseRecoverableKeyStoreBackendCertXmls(cert_xml, sig_xml),
            std::nullopt);
}

TEST_F(RecoverableKeyStoreBackendCertVerifyTest, FailSignatureXmlVerification) {
  std::string cert_xml, sig_xml;
  ASSERT_TRUE(base::Base64Decode(kCertXml10014B64, &cert_xml));
  ASSERT_TRUE(base::Base64Decode(kInvalidSigXmlB64, &sig_xml));

  ExpectSendVerifyAndParseBackendCertResult(
      VerifyAndParseBackendCertResult::kVerifySignatureFailed);
  EXPECT_EQ(VerifyAndParseRecoverableKeyStoreBackendCertXmls(cert_xml, sig_xml),
            std::nullopt);
}

TEST_F(RecoverableKeyStoreBackendCertVerifyTest,
       FailCertSignatureVerification) {
  std::string cert_xml, sig_xml;
  ASSERT_TRUE(base::Base64Decode(kCertXml10014B64, &cert_xml));
  ASSERT_TRUE(base::Base64Decode(kSigXml10013B64, &sig_xml));

  ExpectSendVerifyAndParseBackendCertResult(
      VerifyAndParseBackendCertResult::kVerifyCertFileSignatureFailed);
  EXPECT_EQ(VerifyAndParseRecoverableKeyStoreBackendCertXmls(cert_xml, sig_xml),
            std::nullopt);
}

// Practically, if the signature verification of the xml file succeeds, the
// remaining steps will likely succeed too. Those code paths are harder to test
// because the server never signed an invalid cert xml.

}  // namespace
}  // namespace cryptohome
