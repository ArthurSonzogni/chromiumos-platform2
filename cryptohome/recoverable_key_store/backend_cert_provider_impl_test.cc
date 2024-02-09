// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/recoverable_key_store/backend_cert_provider_impl.h"

#include <string>
#include <utility>

#include <attestation/proto_bindings/pca_agent.pb.h>
#include <base/base64.h>
#include <base/files/file_path.h>
#include <base/functional/callback.h>
#include <base/memory/ptr_util.h>
#include <base/test/task_environment.h>
#include <brillo/secure_blob.h>
#include <dbus/mock_object_proxy.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <pca_agent-client/pca_agent/dbus-proxies.h>
#include <pca_agent-client-test/pca_agent/dbus-proxy-mocks.h>

#include "cryptohome/error/cryptohome_error.h"
#include "cryptohome/fake_platform.h"
#include "cryptohome/recoverable_key_store/backend_cert_test_constants.h"
#include "cryptohome/recoverable_key_store/type.h"

namespace cryptohome {

// Peer class to control a RecoverableKeyStoreBackendCertProviderImpl, exposing
// some private methods we want to test.
class RecoverableKeyStoreBackendProviderPeer {
 public:
  RecoverableKeyStoreBackendProviderPeer(
      libstorage::Platform* platform,
      std::unique_ptr<org::chromium::RksAgentProxyInterface> fetcher)
      : provider_(
            base::WrapUnique(new RecoverableKeyStoreBackendCertProviderImpl(
                platform, std::move(fetcher)))) {}

  // Methods to execute RecoverableKeyStoreBackendCertProviderImpl public
  // methods.

  std::optional<RecoverableKeyStoreBackendCert> GetBackendCert() const {
    return provider_->GetBackendCert();
  }

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
using ::attestation::pca_agent::RksCertificateAndSignature;
using ::testing::Contains;
using ::testing::DoAll;
using ::testing::Field;
using ::testing::NiceMock;
using ::testing::SaveArg;

class RecoverableKeyStoreBackendCertProviderTest : public ::testing::Test {
 public:
  void SetUp() override {
    object_proxy_ =
        new dbus::MockObjectProxy(nullptr, "", dbus::ObjectPath("/"));
    ON_CALL(*object_proxy_, DoWaitForServiceToBeAvailable)
        .WillByDefault([](auto* callback) { std::move(*callback).Run(true); });

    auto fetcher =
        std::make_unique<NiceMock<org::chromium::RksAgentProxyMock>>();
    fetcher_ = fetcher.get();
    ON_CALL(*fetcher_, GetObjectProxy())
        .WillByDefault(Return(object_proxy_.get()));
    ON_CALL(*fetcher_, DoRegisterCertificateFetchedSignalHandler)
        .WillByDefault(
            [&](const auto& on_fetcher_signal, auto* on_fetcher_connected) {
              on_fetcher_signal_ = on_fetcher_signal;
              on_fetcher_connected_ = std::move(*on_fetcher_connected);
            });

    provider_ = std::make_unique<RecoverableKeyStoreBackendProviderPeer>(
        &platform_, std::move(fetcher));
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

  void InitFetcherEmpty() {
    ON_CALL(*fetcher_, GetCertificate)
        .WillByDefault(DoAll(SetArgPointee<0>(RksCertificateAndSignature()),
                             Return(true)));
    std::move(on_fetcher_connected_).Run("", "", true);
  }

  void InitFetcherWithCert(const std::string& cert_xml,
                           const std::string& sig_xml) {
    RksCertificateAndSignature reply;
    reply.set_certificate_xml(cert_xml);
    reply.set_signature_xml(sig_xml);
    ON_CALL(*fetcher_, GetCertificate)
        .WillByDefault(DoAll(SetArgPointee<0>(reply), Return(true)));
    std::move(on_fetcher_connected_).Run("", "", true);
  }

  void SendCertificateFetched(const std::string& cert_xml,
                              const std::string& sig_xml) {
    RksCertificateAndSignature reply;
    reply.set_certificate_xml(cert_xml);
    reply.set_signature_xml(sig_xml);
    on_fetcher_signal_.Run(reply);
  }

  base::test::SingleThreadTaskEnvironment task_environment_ = {
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};

  FakePlatform platform_;

  scoped_refptr<dbus::MockObjectProxy> object_proxy_;
  NiceMock<org::chromium::RksAgentProxyMock>* fetcher_;

  std::unique_ptr<RecoverableKeyStoreBackendProviderPeer> provider_;

 private:
  base::RepeatingCallback<void(const RksCertificateAndSignature&)>
      on_fetcher_signal_;
  base::OnceCallback<void(const std::string&, const std::string&, bool)>
      on_fetcher_connected_;
};

TEST_F(RecoverableKeyStoreBackendCertProviderTest, UpdateCerts) {
  // At the start, there are no loaded certs.
  EXPECT_FALSE(provider_->GetBackendCert().has_value());

  // Update with cert list version 10013.
  CertificateListData data_10013;
  Get10013Data(&data_10013);
  provider_->OnCertificateFetched(data_10013.cert_xml, data_10013.sig_xml);
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
  provider_->OnCertificateFetched(data_10014.cert_xml, data_10014.sig_xml);
  cert = provider_->GetBackendCert();
  ASSERT_TRUE(cert.has_value());
  EXPECT_EQ(cert->version, data_10014.list.version);
  EXPECT_THAT(
      data_10014.list.certs,
      Contains(Field(&RecoverableKeyStoreCert::public_key, cert->public_key)));

  // Update back with cert list version 10013 won't take affect.
  provider_->OnCertificateFetched(data_10013.cert_xml, data_10013.sig_xml);
  cert = provider_->GetBackendCert();
  ASSERT_TRUE(cert.has_value());
  EXPECT_EQ(cert->version, data_10014.list.version);

  // Failed to verify and parse new certs will not break existing state.
  provider_->OnCertificateFetched("not a xml", "not a xml");
  cert = provider_->GetBackendCert();
  ASSERT_TRUE(cert.has_value());
  EXPECT_EQ(cert->version, data_10014.list.version);

  // Certs are persisted.
  SetUp();
  cert = provider_->GetBackendCert();
  ASSERT_TRUE(cert.has_value());
  EXPECT_EQ(cert->version, data_10014.list.version);
}

TEST_F(RecoverableKeyStoreBackendCertProviderTest,
       StartFetchingEmptyCertInitially) {
  // At the start, there are no loaded certs.
  EXPECT_FALSE(provider_->GetBackendCert().has_value());

  InitFetcherEmpty();

  // Still no certs.
  EXPECT_FALSE(provider_->GetBackendCert().has_value());

  // Updating with cert list 10013.
  CertificateListData data_10013;
  Get10013Data(&data_10013);
  SendCertificateFetched(data_10013.cert_xml, data_10013.sig_xml);

  std::optional<RecoverableKeyStoreBackendCert> cert =
      provider_->GetBackendCert();
  ASSERT_TRUE(cert.has_value());
  EXPECT_EQ(cert->version, data_10013.list.version);
  EXPECT_THAT(
      data_10013.list.certs,
      Contains(Field(&RecoverableKeyStoreCert::public_key, cert->public_key)));

  // Updating with cert list 10014.
  CertificateListData data_10014;
  Get10014Data(&data_10014);
  SendCertificateFetched(data_10014.cert_xml, data_10014.sig_xml);

  cert = provider_->GetBackendCert();
  ASSERT_TRUE(cert.has_value());
  EXPECT_EQ(cert->version, data_10014.list.version);
  EXPECT_THAT(
      data_10014.list.certs,
      Contains(Field(&RecoverableKeyStoreCert::public_key, cert->public_key)));
}

TEST_F(RecoverableKeyStoreBackendCertProviderTest,
       StartFetchingHasCertInitially) {
  // At the start, there are no loaded certs.
  EXPECT_FALSE(provider_->GetBackendCert().has_value());

  // Fetcher has already fetched the version 10013 cert when the provider
  // connects to the signal.
  CertificateListData data_10013;
  Get10013Data(&data_10013);
  InitFetcherWithCert(data_10013.cert_xml, data_10013.sig_xml);

  std::optional<RecoverableKeyStoreBackendCert> cert =
      provider_->GetBackendCert();
  ASSERT_TRUE(cert.has_value());
  EXPECT_EQ(cert->version, data_10013.list.version);
  EXPECT_THAT(
      data_10013.list.certs,
      Contains(Field(&RecoverableKeyStoreCert::public_key, cert->public_key)));

  // Updating with cert list 10014.
  CertificateListData data_10014;
  Get10014Data(&data_10014);
  SendCertificateFetched(data_10014.cert_xml, data_10014.sig_xml);

  cert = provider_->GetBackendCert();
  ASSERT_TRUE(cert.has_value());
  EXPECT_EQ(cert->version, data_10014.list.version);
  EXPECT_THAT(
      data_10014.list.certs,
      Contains(Field(&RecoverableKeyStoreCert::public_key, cert->public_key)));
}

}  // namespace
}  // namespace cryptohome
