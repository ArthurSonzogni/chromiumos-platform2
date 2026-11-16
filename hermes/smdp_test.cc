// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hermes/smdp.h"

#include <memory>
#include <utility>

#include <base/functional/bind.h>
#include <base/location.h>
#include <base/threading/thread.h>
#include <base/time/time.h>
#include <brillo/http/mock_transport.h>
#include <gmock/gmock.h>
#include <google-lpa/lpa/data/proto/euicc_spec_version.pb.h>
#include <gtest/gtest.h>

#include "hermes/logger.h"
#include "hermes/mock_executor.h"

namespace hermes {

TEST(SmdpTest, Sgp22V25EnabledFlag) {
  lpa::proto::EuiccSpecVersion version;
  version.set_major(2);
  version.set_minor(2);
  version.set_revision(0);

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

}  // namespace hermes
