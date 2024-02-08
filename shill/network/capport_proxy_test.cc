// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/network/capport_proxy.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/functional/bind.h>
#include <base/functional/callback_helpers.h>
#include <base/time/time.h>
#include <brillo/http/http_request.h>
#include <brillo/http/http_transport_fake.h>
#include <brillo/http/mock_transport.h>
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <net-base/http_url.h>

namespace shill {
namespace {

using testing::_;
using testing::Eq;

const net_base::HttpUrl kApiUrl = *net_base::HttpUrl::CreateFromString(
    "https://example.org/captive-portal/api/X54PD39JV");
constexpr char kInterfaceName[] = "wlan0";
constexpr char kUserPortalUrl[] = "https://example.org/portal.html";
constexpr char kVenueInfoUrl[] = "https://flight.example.com/entertainment";

// Used to verify the callback of CapportProxy.
class MockCapportClient {
 public:
  MOCK_METHOD(void, OnStatusReceived, (const std::optional<CapportStatus>&));
};

}  // namespace

TEST(CapportStatusTest, ParseFromJsonSuccess) {
  const std::string json = R"({
   "captive": false,
   "user-portal-url": "https://example.org/portal.html",
   "venue-info-url": "https://flight.example.com/entertainment",
   "seconds-remaining": 326,
   "bytes-remaining": 65536,
   "can-extend-session": true
})";

  const CapportStatus expected{
      .is_captive = false,
      .user_portal_url =
          net_base::HttpUrl::CreateFromString(kUserPortalUrl).value(),
      .venue_info_url =
          net_base::HttpUrl::CreateFromString(kVenueInfoUrl).value(),
      .can_extend_session = true,
      .seconds_remaining = base::Seconds(326),
      .bytes_remaining = 65536,
  };

  EXPECT_EQ(CapportStatus::ParseFromJson(json).value(), expected);
}

TEST(CapportStatusTest, ParseFromJsonMissingOptionalField) {
  const std::string json = R"({
   "captive": true,
   "user-portal-url": "https://example.org/portal.html"
})";

  const CapportStatus expected{
      .is_captive = true,
      .user_portal_url =
          net_base::HttpUrl::CreateFromString(kUserPortalUrl).value(),
      .venue_info_url = std::nullopt,
      .can_extend_session = std::nullopt,
      .seconds_remaining = std::nullopt,
      .bytes_remaining = std::nullopt,
  };

  EXPECT_EQ(CapportStatus::ParseFromJson(json).value(), expected);
}

TEST(CapportStatusTest, ParseFromJsonMissingRequiredField) {
  // Miss "captive" field.
  const std::string json = R"({
   "user-portal-url": "https://example.org/portal.html",
   "venue-info-url": "https://flight.example.com/entertainment",
   "seconds-remaining": 326,
   "bytes-remaining": 65536,
   "can-extend-session": true
})";

  EXPECT_FALSE(CapportStatus::ParseFromJson(json).has_value());
}

TEST(CapportStatusTest, ParseFromJsonInvalidUserPortalUrl) {
  // The user portal URL must be HTTPS, HTTP is considered invalid.
  const std::string json = R"({
   "captive": true,
   "user-portal-url": "http://example.org/portal.html"
})";

  EXPECT_FALSE(CapportStatus::ParseFromJson(json).has_value());
}

TEST(CapportStatusTest, ParseFromJsonMissingUserPortalUrl) {
  // The user portal URL must be HTTPS, HTTP is considered invalid.
  const std::string json = R"({
   "captive": true,
})";

  EXPECT_FALSE(CapportStatus::ParseFromJson(json).has_value());
}

TEST(CapportStatusTest, ParseFromJsonInvalidVenueInfoUrl) {
  const std::string json = R"({
   "captive": true,
   "venue-info-url": "invalid URL",
})";

  EXPECT_FALSE(CapportStatus::ParseFromJson(json).has_value());
}

TEST(CapportProxyTest, SendRequest) {
  // Verify if SendRequest() sends the expected HTTP request.
  const std::vector<std::pair<std::string, std::string>> kHeaders = {
      {"Accept", "application/captive+json"}};
  auto mock_transport = std::make_shared<brillo::http::MockTransport>();
  EXPECT_CALL(*mock_transport, SetInterface(kInterfaceName));
  EXPECT_CALL(
      *mock_transport,
      CreateConnection(kApiUrl.ToString(), brillo::http::request_type::kGet,
                       kHeaders, _, _, _));

  auto proxy = CapportProxy::Create(kInterfaceName, kApiUrl, mock_transport);
  EXPECT_NE(proxy, nullptr);

  proxy->SendRequest(base::DoNothing());
}

TEST(CapportProxyTest, SendRequestSuccess) {
  const std::string json_str = R"({
   "captive": true,
   "user-portal-url": "https://example.org/portal.html"
})";
  const CapportStatus status{
      .is_captive = true,
      .user_portal_url =
          net_base::HttpUrl::CreateFromString(kUserPortalUrl).value(),
      .venue_info_url = std::nullopt,
      .can_extend_session = std::nullopt,
      .seconds_remaining = std::nullopt,
      .bytes_remaining = std::nullopt,
  };

  auto fake_transport = std::make_shared<brillo::http::fake::Transport>();
  auto proxy = CapportProxy::Create(kInterfaceName, kApiUrl, fake_transport);
  fake_transport->AddSimpleReplyHandler(
      kApiUrl.ToString(), brillo::http::request_type::kGet,
      brillo::http::status_code::Ok, json_str, "application/captive+json");

  // When the HTTP server replies a valid JSON string, the client should get
  // the valid status via callback.
  MockCapportClient client;
  EXPECT_CALL(client, OnStatusReceived(Eq(status))).Times(2);
  proxy->SendRequest(base::BindOnce(&MockCapportClient::OnStatusReceived,
                                    base::Unretained(&client)));
  proxy->SendRequest(base::BindOnce(&MockCapportClient::OnStatusReceived,
                                    base::Unretained(&client)));
}

TEST(CapportProxyTest, SendRequestFail) {
  auto fake_transport = std::make_shared<brillo::http::fake::Transport>();
  auto proxy = CapportProxy::Create(kInterfaceName, kApiUrl, fake_transport);
  fake_transport->AddSimpleReplyHandler(
      kApiUrl.ToString(), brillo::http::request_type::kGet,
      brillo::http::status_code::Ok, "Invalid JSON string",
      "application/captive+json");

  // When the HTTP server replies an invalid JSON string, the client should get
  // std::nullopt via callback.
  MockCapportClient client;
  EXPECT_CALL(client, OnStatusReceived(Eq(std::nullopt)));
  proxy->SendRequest(base::BindOnce(&MockCapportClient::OnStatusReceived,
                                    base::Unretained(&client)));
}

TEST(CapportProxyTest, SendRequestAndStop) {
  auto fake_transport = std::make_shared<brillo::http::fake::Transport>();
  auto proxy = CapportProxy::Create(kInterfaceName, kApiUrl, fake_transport);
  fake_transport->SetAsyncMode(true);
  fake_transport->AddSimpleReplyHandler(
      kApiUrl.ToString(), brillo::http::request_type::kGet,
      brillo::http::status_code::Ok, "Invalid JSON string",
      "application/captive+json");

  // When stopping proxy before the transport is done, the client should not get
  // callback.
  MockCapportClient client;
  EXPECT_CALL(client, OnStatusReceived).Times(0);

  proxy->SendRequest(base::BindOnce(&MockCapportClient::OnStatusReceived,
                                    base::Unretained(&client)));
  proxy->Stop();
  fake_transport->HandleAllAsyncRequests();  // Simulate the transport is done.
}

}  // namespace shill
