// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "attestation/pca_agent/server/rks_cert_fetcher.h"

#include <base/test/repeating_test_future.h>
#include <base/test/task_environment.h>
#include <brillo/variant_dictionary.h>
#include <dbus/mock_object_proxy.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <libstorage/platform/fake_platform.h>
#include <libstorage/platform/platform.h>
#include <shill/dbus-constants.h>
#include <shill/dbus-proxy-mocks.h>

#include "attestation/pca_agent/server/fake_transport_factory.h"
#include "attestation/pca_agent/server/mock_pca_http_utils.h"

namespace attestation {
namespace pca_agent {

namespace {

using ::org::chromium::flimflam::ManagerProxyMock;
using ::testing::_;
using ::testing::DoAll;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::SetArgPointee;
using ::testing::Unused;

using OnCertFetchedCallback =
    base::RepeatingCallback<void(const RksCertificateAndSignature&)>;
using OnCertFetchedFuture =
    base::test::RepeatingTestFuture<const RksCertificateAndSignature&>;

constexpr char kCertXmlUrl[] =
    "https://www.gstatic.com/cryptauthvault/v0/cert.xml";
constexpr char kSignatureXmlUrl[] =
    "https://www.gstatic.com/cryptauthvault/v0/cert.sig.xml";

constexpr char kFakeCertXml[] = "certificate";
constexpr char kFakeSignatureXml[] = "signature";

class RksCertificateFetcherTest : public ::testing::Test {
  void SetUp() override {
    auto manager_proxy = std::make_unique<NiceMock<ManagerProxyMock>>();
    manager_proxy_ = manager_proxy.get();

    object_proxy_ =
        new dbus::MockObjectProxy(nullptr, "", dbus::ObjectPath("/"));
    ON_CALL(*object_proxy_, DoWaitForServiceToBeAvailable)
        .WillByDefault([](auto* callback) { std::move(*callback).Run(true); });

    ON_CALL(*manager_proxy_, GetObjectProxy())
        .WillByDefault(Return(object_proxy_.get()));
    ON_CALL(*manager_proxy_, DoRegisterPropertyChangedSignalHandler)
        .WillByDefault(
            [&](const auto& on_manager_signal, auto* on_manager_connected) {
              on_manager_signal_ = on_manager_signal;
              on_manager_connected_ = std::move(*on_manager_connected);
            });

    ON_CALL(mock_pca_http_utils_, GetChromeProxyServersAsync(_, _))
        .WillByDefault(
            [](Unused, auto callback) { std::move(callback).Run(true, {}); });

    fetcher_ = std::make_unique<RksCertificateFetcher>(
        &platform_, std::move(manager_proxy));
    fetcher_->set_transport_factory_for_testing(&fake_trasport_factory_);
    fetcher_->set_pca_http_utils_for_testing(&mock_pca_http_utils_);
  }

 protected:
  // Calls |StartFetching| with the injected test future callback.
  void StartFetching() {
    fetcher_->StartFetching(on_cert_fetched_future_.GetCallback());
  }

  void ExpectCertsFetched() {
    ASSERT_FALSE(on_cert_fetched_future_.IsEmpty());
    RksCertificateAndSignature reply = on_cert_fetched_future_.Take();
    EXPECT_EQ(reply.certificate_xml(), kFakeCertXml);
    EXPECT_EQ(reply.signature_xml(), kFakeSignatureXml);
    EXPECT_TRUE(on_cert_fetched_future_.IsEmpty());
    RksCertificateAndSignature persisted_cert = fetcher_->GetCertificate();
    EXPECT_EQ(persisted_cert.certificate_xml(), kFakeCertXml);
    EXPECT_EQ(persisted_cert.signature_xml(), kFakeSignatureXml);
  }

  // Functions to connect the manager signal with online/offline status
  // initially.
  void InitOnline() {
    ON_CALL(*manager_proxy_, GetProperties)
        .WillByDefault(DoAll(SetArgPointee<0>(brillo::VariantDictionary{
                                 {shill::kConnectionStateProperty,
                                  std::string(shill::kStateOnline)}}),
                             Return(true)));
    std::move(on_manager_connected_).Run("", "", true);
  }
  void InitOffline() {
    ON_CALL(*manager_proxy_, GetProperties)
        .WillByDefault(DoAll(SetArgPointee<0>(brillo::VariantDictionary{
                                 {shill::kConnectionStateProperty,
                                  std::string(shill::kStateIdle)}}),
                             Return(true)));
    std::move(on_manager_connected_).Run("", "", true);
  }

  // Functions to send shill signals from the manager.
  void SendOnlineSignal() {
    on_manager_signal_.Run(shill::kConnectionStateProperty,
                           std::string(shill::kStateOnline));
  }
  void SendOfflineSignal() {
    on_manager_signal_.Run(shill::kConnectionStateProperty,
                           std::string(shill::kStateIdle));
  }

  // Functions to set expectations (handlers) on the fake transport.
  void ExpectGetUrl(const std::string& url,
                    int status_code,
                    const std::string reply) {
    fake_trasport_factory_.get_fake_transport(brillo::http::kDirectProxy)
        ->AddSimpleReplyHandler(url, brillo::http::request_type::kGet,
                                status_code, reply, "");
  }
  void ExpectGetCertSuccess() {
    ExpectGetUrl(kCertXmlUrl, brillo::http::status_code::Ok, kFakeCertXml);
  }
  void ExpectGetSignatureSuccess() {
    ExpectGetUrl(kSignatureXmlUrl, brillo::http::status_code::Ok,
                 kFakeSignatureXml);
  }
  void ExpectGetCertFailed() {
    ExpectGetUrl(kCertXmlUrl, brillo::http::status_code::NotFound, "");
  }

  base::test::SingleThreadTaskEnvironment task_environment_ = {
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};

  OnCertFetchedFuture on_cert_fetched_future_;

  FakeTransportFactory fake_trasport_factory_;
  MockPcaHttpUtils mock_pca_http_utils_;

  libstorage::FakePlatform platform_;

  NiceMock<ManagerProxyMock>* manager_proxy_;

  std::unique_ptr<RksCertificateFetcher> fetcher_;

 private:
  scoped_refptr<dbus::MockObjectProxy> object_proxy_;

  base::RepeatingCallback<void(const std::string&, const brillo::Any&)>
      on_manager_signal_;
  base::OnceCallback<void(const std::string&, const std::string&, bool)>
      on_manager_connected_;
};

TEST_F(RksCertificateFetcherTest, FetchCerts) {
  StartFetching();

  ExpectGetCertSuccess();
  ExpectGetSignatureSuccess();
  InitOnline();

  // Because the initial state is online, certs will immediately be fetched.
  ExpectCertsFetched();

  // Certs will be fetched again after 1 day.
  task_environment_.FastForwardBy(base::Days(1));
  ExpectCertsFetched();
}

TEST_F(RksCertificateFetcherTest, FetchCertsFailure) {
  StartFetching();

  ExpectGetCertFailed();
  InitOnline();

  // Because the initial state is online, certs will immediately be fetched, but
  // the attempt failed.
  EXPECT_TRUE(on_cert_fetched_future_.IsEmpty());

  // On failure, certs will be fetched again after 10 minutes.
  ExpectGetCertSuccess();
  ExpectGetSignatureSuccess();
  task_environment_.FastForwardBy(base::Minutes(10));
  ExpectCertsFetched();
}

// Test that certificate fetch requests are sent at correct timings given the
// interaction of periodic fetch interval and the network state.
TEST_F(RksCertificateFetcherTest, FetchCertsStateControl) {
  StartFetching();
  InitOffline();

  // Assume fetches are all successful in this test, such that each fetch
  // attempt will result in callback being triggered once.
  ExpectGetCertSuccess();
  ExpectGetSignatureSuccess();

  // Because the initial state is offline, certs won't be fetched.
  EXPECT_TRUE(on_cert_fetched_future_.IsEmpty());

  // The moment network is online, certs will be fetched.
  SendOnlineSignal();
  ExpectCertsFetched();

  // Haven't reached the 1 day interval, so certs won't be fetched again.
  task_environment_.FastForwardBy(base::Hours(12));
  EXPECT_TRUE(on_cert_fetched_future_.IsEmpty());

  // Assume network becomes offline now. Even if it passes the 1 day interval,
  // certs can't be fetched.
  SendOfflineSignal();
  task_environment_.FastForwardBy(base::Hours(14));
  EXPECT_TRUE(on_cert_fetched_future_.IsEmpty());

  // The moment network becomes online, certs will be fetched immediately.
  SendOnlineSignal();
  ExpectCertsFetched();

  // If the 1 day interval hasn't passed, even if the network turns offline and
  // online again, certs won't be fetched.
  SendOfflineSignal();
  task_environment_.FastForwardBy(base::Hours(14));
  SendOnlineSignal();
  EXPECT_TRUE(on_cert_fetched_future_.IsEmpty());
}

}  // namespace
}  // namespace pca_agent
}  // namespace attestation
