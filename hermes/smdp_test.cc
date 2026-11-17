// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hermes/smdp.h"

#include <memory>
#include <string>
#include <utility>

#include <base/functional/bind.h>
#include <base/location.h>
#include <base/threading/thread.h>
#include <base/time/time.h>
#include <brillo/errors/error.h>
#include <brillo/http/http_request.h>
#include <brillo/http/mock_connection.h>
#include <brillo/http/mock_transport.h>
#include <gmock/gmock.h>
#include <google-lpa/lpa/data/proto/euicc_spec_version.pb.h>
#include <gtest/gtest.h>

#include "hermes/hermes_common.h"
#include "hermes/logger.h"
#include "hermes/mock_executor.h"

namespace hermes {

namespace {

class TestSmdp : public Smdp {
 public:
  using Smdp::SendHttps;
  using Smdp::Smdp;
};

}  // namespace

TEST(SmdpTest, Sgp22V25EnabledFlag) {
  lpa::proto::EuiccSpecVersion version;
  version.set_major(kLegacySgp22SpecMajor);
  version.set_minor(kLegacySgp22SpecMinor);
  version.set_revision(kLegacySgp22SpecRevision);

  Logger logger;
  MockExecutor executor;
  auto transport = std::make_shared<brillo::http::MockTransport>();
  EXPECT_CALL(
      *transport,
      UseCustomCertificate(brillo::http::Transport::Certificate::kHermesProd))
      .Times(1);

  Smdp smdp("smdp.example.com", "/usr/share/hermes-ca-certificates/prod",
            version, &logger, &executor, transport);
  EXPECT_EQ(smdp.logger(), &logger);
  EXPECT_FALSE(smdp.sgp22_v25_enabled());

  smdp.SetSgp22V25Enabled(true);
  EXPECT_TRUE(smdp.sgp22_v25_enabled());

  smdp.SetSgp22V25Enabled(false);
  EXPECT_FALSE(smdp.sgp22_v25_enabled());
}

TEST(SmdpTest, NullLoggerFallsBackToBaseLogger) {
  lpa::proto::EuiccSpecVersion version;
  MockExecutor executor;
  auto transport = std::make_shared<brillo::http::MockTransport>();
  EXPECT_CALL(
      *transport,
      UseCustomCertificate(brillo::http::Transport::Certificate::kHermesTest))
      .Times(1);

  Smdp smdp("smdp.example.com", "/usr/share/hermes-ca-certificates/test/",
            version, nullptr, &executor, transport);
  EXPECT_NE(smdp.logger(), nullptr);
}

TEST(SmdpTest, DestructorReleasesTransportOnExecutorThread) {
  lpa::proto::EuiccSpecVersion version;
  Logger logger;
  MockExecutor executor;

  // On-thread destruction releases server_transport_ synchronously.
  {
    auto transport = std::make_shared<brillo::http::MockTransport>();
    std::weak_ptr<brillo::http::Transport> weak_transport = transport;
    EXPECT_CALL(
        *transport,
        UseCustomCertificate(brillo::http::Transport::Certificate::kHermesProd))
        .Times(1);
    auto smdp = std::make_unique<Smdp>(
        "smdp.example.com", "/usr/share/hermes-ca-certificates/prod", version,
        &logger, &executor, std::move(transport));
    smdp.reset();
    EXPECT_TRUE(weak_transport.expired());
  }

  // Off-thread destruction (e.g. from Lpa::lpa_executor_) defers releasing
  // server_transport_ onto executor_.
  {
    auto transport = std::make_shared<brillo::http::MockTransport>();
    std::weak_ptr<brillo::http::Transport> weak_transport = transport;
    EXPECT_CALL(
        *transport,
        UseCustomCertificate(brillo::http::Transport::Certificate::kHermesProd))
        .Times(1);
    auto smdp = std::make_unique<Smdp>(
        "smdp.example.com", "/usr/share/hermes-ca-certificates/prod", version,
        &logger, &executor, std::move(transport));
    base::Thread worker("smdp_dtor_worker");
    ASSERT_TRUE(worker.Start());
    worker.task_runner()->PostTask(
        FROM_HERE, base::BindOnce([](std::unique_ptr<Smdp> s) { s.reset(); },
                                  std::move(smdp)));
    worker.Stop();
    EXPECT_FALSE(weak_transport.expired());
    executor.FastForwardBy(base::TimeDelta());
    EXPECT_TRUE(weak_transport.expired());
  }
}

TEST(SmdpTest, AdminProtocolHeaderSentInHttpsRequest) {
  using ::testing::_;
  using ::testing::Contains;
  using ::testing::Invoke;
  using ::testing::Pair;
  using ::testing::Return;

  lpa::proto::EuiccSpecVersion version;
  version.set_major(kLegacySgp22SpecMajor);
  version.set_minor(kLegacySgp22SpecMinor);
  version.set_revision(kLegacySgp22SpecRevision);

  Logger logger;
  MockExecutor executor;
  auto transport = std::make_shared<brillo::http::MockTransport>();
  EXPECT_CALL(
      *transport,
      UseCustomCertificate(brillo::http::Transport::Certificate::kHermesProd))
      .Times(1);
  EXPECT_CALL(*transport, SetDefaultTimeout(_)).Times(2);

  auto make_mock_connection = [&transport]() {
    auto conn = std::make_shared<brillo::http::MockConnection>(transport);
    EXPECT_CALL(*conn, MockSetRequestData(_, _)).WillOnce(Return(true));
    EXPECT_CALL(*conn, FinishRequestAsync(_, _))
        .WillOnce(Invoke([](brillo::http::SuccessCallback /*success_cb*/,
                            brillo::http::ErrorCallback error_cb) {
          auto err = brillo::Error::Create(FROM_HERE, "test", "done",
                                           "stop after header check");
          std::move(error_cb).Run(1, err.get());
          return 1;
        }));
    return conn;
  };

  TestSmdp smdp("smdp.example.com", "/usr/share/hermes-ca-certificates/prod",
                version, &logger, &executor, transport);

  // Default sgp22_v25_enabled is false -> SendHttps sends legacy v2.2.2 header.
  auto legacy_conn = make_mock_connection();
  EXPECT_CALL(
      *transport,
      CreateConnection(
          "https://smdp.example.com/gsma/rsp2/es9plus/initiateAuthentication",
          brillo::http::request_type::kPost,
          Contains(Pair("X-Admin-Protocol", kLegacyAdminProtocolVersion)),
          "gsma-rsp-lpad", _, _))
      .WillOnce(Return(legacy_conn));
  bool cb_called = false;
  smdp.SendHttps("/gsma/rsp2/es9plus/initiateAuthentication", "{}",
                 [&cb_called](int /*code*/, std::string& /*resp*/, int err) {
                   EXPECT_EQ(err, lpa::smdp::SmdpClient::kSendHttpsError);
                   cb_called = true;
                 });
  EXPECT_TRUE(cb_called);

  // When sgp22_v25_enabled is true -> SendHttps sends SGP.22 v2.5.0 header.
  smdp.SetSgp22V25Enabled(true);
  auto v25_conn = make_mock_connection();
  EXPECT_CALL(
      *transport,
      CreateConnection(
          "https://smdp.example.com/gsma/rsp2/es9plus/initiateAuthentication",
          brillo::http::request_type::kPost,
          Contains(Pair("X-Admin-Protocol", kSgp22AdminProtocolVersion)),
          "gsma-rsp-lpad", _, _))
      .WillOnce(Return(v25_conn));
  cb_called = false;
  smdp.SendHttps("/gsma/rsp2/es9plus/initiateAuthentication", "{}",
                 [&cb_called](int /*code*/, std::string& /*resp*/, int err) {
                   EXPECT_EQ(err, lpa::smdp::SmdpClient::kSendHttpsError);
                   cb_called = true;
                 });
  EXPECT_TRUE(cb_called);
}

}  // namespace hermes
