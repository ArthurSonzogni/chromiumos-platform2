// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/recoverable_key_store/backend_cert_provider_impl.h"

#include <string>
#include <utility>

#include <base/base64.h>
#include <base/files/file_path.h>
#include <base/functional/callback.h>
#include <base/test/task_environment.h>
#include <brillo/secure_blob.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "cryptohome/error/cryptohome_error.h"
#include "cryptohome/fake_platform.h"
#include "cryptohome/recoverable_key_store/backend_cert_test_constants.h"
#include "cryptohome/recoverable_key_store/type.h"

namespace cryptohome {

// Peer class to control a RecoverableKeyStoreBackendCertProviderImpl, exposing
// some private methods we want to test.
class RecoverableKeyStoreBackendProviderPeer {
 public:
  explicit RecoverableKeyStoreBackendProviderPeer(
      std::unique_ptr<RecoverableKeyStoreBackendCertProviderImpl> provider)
      : provider_(std::move(provider)) {}

  // Methods to execute RecoverableKeyStoreBackendCertProviderImpl private
  // methods.

  void OnCertificateFetched(const std::string& cert_xml,
                            const std::string& sig_xml) {
    provider_->OnCertificateFetched(cert_xml, sig_xml);
  }

 private:
  std::unique_ptr<RecoverableKeyStoreBackendCertProviderImpl> provider_;
};

namespace {
using ::testing::Contains;
using ::testing::Field;
using ::testing::SaveArg;

class RecoverableKeyStoreBackendCertProviderTest : public ::testing::Test {
  void SetUp() override {
    auto provider =
        std::make_unique<RecoverableKeyStoreBackendCertProviderImpl>(
            &platform_);
    provider_ = provider.get();
    provider_peer_ = std::make_unique<RecoverableKeyStoreBackendProviderPeer>(
        std::move(provider));
  }

 protected:
  struct CertificateListData {
    std::string cert_xml;
    std::string sig_xml;
    RecoverableKeyStoreCertList list;
  };

  void GetCertificateListData(const std::string& cert_b64,
                              const std::string& sig_b64,
                              CertificateListData* data) {
    std::string cert_xml, sig_xml;
    ASSERT_TRUE(base::Base64Decode(cert_b64, &cert_xml));
    ASSERT_TRUE(base::Base64Decode(sig_b64, &sig_xml));
    std::optional<RecoverableKeyStoreCertList> cert_list =
        VerifyAndParseRecoverableKeyStoreBackendCertXmls(cert_xml, sig_xml);
    ASSERT_TRUE(cert_list.has_value());
    *data = CertificateListData{
        .cert_xml = std::move(cert_xml),
        .sig_xml = std::move(sig_xml),
        .list = std::move(*cert_list),
    };
  }

  void Get10013Data(CertificateListData* data) {
    GetCertificateListData(kCertXml10013B64, kSigXml10013B64, data);
  }

  void Get10014Data(CertificateListData* data) {
    GetCertificateListData(kCertXml10014B64, kSigXml10014B64, data);
  }

  base::test::SingleThreadTaskEnvironment task_environment_ = {
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};

  FakePlatform platform_;

  std::unique_ptr<RecoverableKeyStoreBackendProviderPeer> provider_peer_;
  RecoverableKeyStoreBackendCertProviderImpl* provider_;
};

TEST_F(RecoverableKeyStoreBackendCertProviderTest, UpdateCerts) {
  // At the start, there are no loaded certs.
  EXPECT_FALSE(provider_->GetBackendCert().has_value());

  // Update with cert list version 10013.
  CertificateListData data_10013;
  Get10013Data(&data_10013);
  provider_peer_->OnCertificateFetched(data_10013.cert_xml, data_10013.sig_xml);
  std::optional<RecoverableKeyStoreBackendCert> cert =
      provider_->GetBackendCert();
  ASSERT_TRUE(cert.has_value());
  EXPECT_EQ(cert->version, data_10013.list.version);
  EXPECT_THAT(
      data_10013.list.certs,
      Contains(Field(&RecoverableKeyStoreCert::public_key, cert->public_key)));

  // Update with cert list version 10014.
  CertificateListData data_10014;
  Get10014Data(&data_10014);
  provider_peer_->OnCertificateFetched(data_10014.cert_xml, data_10014.sig_xml);
  cert = provider_->GetBackendCert();
  ASSERT_TRUE(cert.has_value());
  EXPECT_EQ(cert->version, data_10014.list.version);
  EXPECT_THAT(
      data_10014.list.certs,
      Contains(Field(&RecoverableKeyStoreCert::public_key, cert->public_key)));

  // Update back with cert list version 10013 won't take affect.
  provider_peer_->OnCertificateFetched(data_10013.cert_xml, data_10013.sig_xml);
  cert = provider_->GetBackendCert();
  ASSERT_TRUE(cert.has_value());
  EXPECT_EQ(cert->version, data_10014.list.version);

  // Failed to verify and parse new certs will not break existing state.
  provider_peer_->OnCertificateFetched("not a xml", "not a xml");
  cert = provider_->GetBackendCert();
  ASSERT_TRUE(cert.has_value());
  EXPECT_EQ(cert->version, data_10014.list.version);

  // Certs are persisted.
  auto new_provider =
      std::make_unique<RecoverableKeyStoreBackendCertProviderImpl>(&platform_);
  cert = new_provider->GetBackendCert();
  ASSERT_TRUE(cert.has_value());
  EXPECT_EQ(cert->version, data_10014.list.version);
}

}  // namespace
}  // namespace cryptohome
