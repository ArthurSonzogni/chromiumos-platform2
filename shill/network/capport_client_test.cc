// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/network/capport_client.h"

#include <memory>
#include <utility>

#include <base/functional/bind.h>
#include <base/functional/callback_helpers.h>
#include <base/time/time.h>
#include <gtest/gtest.h>
#include <net-base/http_url.h>

#include "shill/network/mock_capport_proxy.h"

namespace shill {
namespace {

using testing::Return;

const net_base::HttpUrl kUserPortalUrl =
    *net_base::HttpUrl::CreateFromString("https://example.org/portal.html");
const net_base::HttpUrl kVenueInfoUrl =
    *net_base::HttpUrl::CreateFromString("https://example.org/info.html");

}  // namespace

class CapportClientTest : public testing::Test {
 public:
  void SetUp() override {
    auto mock_proxy = std::make_unique<MockCapportProxy>();
    mock_proxy_ = mock_proxy.get();
    ON_CALL(*mock_proxy_, IsRunning).WillByDefault(Return(false));

    client_ = std::make_unique<CapportClient>(
        std::move(mock_proxy),
        base::BindRepeating(&CapportClientTest::OnCapportResultReceived,
                            base::Unretained(this)));
  }

  MOCK_METHOD(void, OnCapportResultReceived, (const CapportClient::Result&));

 protected:
  MockCapportProxy* mock_proxy_;
  std::unique_ptr<CapportClient> client_;
};

TEST_F(CapportClientTest, QueryCapport) {
  EXPECT_CALL(*mock_proxy_, SendRequest)
      .WillOnce([](CapportProxy::StatusCallback callback) {
        const CapportStatus status{
            .is_captive = true,
            .user_portal_url = kUserPortalUrl,
            .venue_info_url = kVenueInfoUrl,
            .can_extend_session = std::nullopt,
            .seconds_remaining = std::nullopt,
            .bytes_remaining = std::nullopt,
        };
        std::move(callback).Run(status);
      });
  EXPECT_CALL(*this, OnCapportResultReceived(CapportClient::Result{
                         .state = CapportClient::State::kClosed,
                         .user_portal_url = kUserPortalUrl,
                         .venue_info_url = kVenueInfoUrl,
                     }));

  client_->QueryCapport();
}

TEST_F(CapportClientTest, QueryCapportFailed) {
  EXPECT_CALL(*mock_proxy_, SendRequest)
      .WillOnce([](CapportProxy::StatusCallback callback) {
        std::move(callback).Run(std::nullopt);
      });
  EXPECT_CALL(*this, OnCapportResultReceived(CapportClient::Result{
                         .state = CapportClient::State::kFailed,
                         .user_portal_url = std::nullopt,
                         .venue_info_url = std::nullopt,
                     }));

  client_->QueryCapport();
}

}  // namespace shill
