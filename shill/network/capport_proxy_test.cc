// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/network/capport_proxy.h"

#include <memory>
#include <string>
#include <string_view>
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

#include "shill/mock_metrics.h"

namespace shill {
namespace {

using testing::_;
using testing::AtMost;
using testing::ElementsAre;
using testing::Eq;

const net_base::HttpUrl kApiUrl = *net_base::HttpUrl::CreateFromString(
    "https://example.org/captive-portal/api/X54PD39JV");
constexpr char kInterfaceName[] = "wlan0";
constexpr char kUserPortalUrl[] = "https://example.org/portal.html";
constexpr char kVenueInfoUrl[] = "https://flight.example.com/entertainment";
const net_base::IPAddress kDnsServers[] = {
    *net_base::IPAddress::CreateFromString("8.8.8.8"),
    *net_base::IPAddress::CreateFromString("8.8.4.4"),
};

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
  // The user portal URL should exists when captive is true.
  const std::string json = R"({
   "captive": true
})";

  EXPECT_FALSE(CapportStatus::ParseFromJson(json).has_value());
}

TEST(CapportStatusTest, ParseFromJsonInvalidVenueInfoUrl) {
  const std::string json = R"({
   "captive": true,
   "venue-info-url": "invalid URL"
})";

  EXPECT_FALSE(CapportStatus::ParseFromJson(json).has_value());
}

TEST(CapportStatusTest, ClearRemainingFieldWhenPortalClose) {
  const std::string json_str = R"({
   "captive": true,
   "user-portal-url": "https://example.org/portal.html",
   "seconds-remaining": 326,
   "bytes-remaining": 65536
})";

  // seconds_remaining and bytes_remaining should be std::nullopt when
  // the portal is captive.
  const CapportStatus expected{
      .is_captive = true,
      .user_portal_url =
          net_base::HttpUrl::CreateFromString(kUserPortalUrl).value(),
      .seconds_remaining = std::nullopt,
      .bytes_remaining = std::nullopt,
  };

  EXPECT_EQ(CapportStatus::ParseFromJson(json_str).value(), expected);
}

TEST(CapportStatusTest, InvalidMinusRemaining) {
  const std::string json = R"({
   "captive": false,
   "seconds-remaining": -326,
   "bytes-remaining": -65536
})";

  const CapportStatus expected{
      .is_captive = false,
      .seconds_remaining = std::nullopt,
      .bytes_remaining = std::nullopt,
  };

  EXPECT_EQ(CapportStatus::ParseFromJson(json).value(), expected);
}

class CapportProxyTest : public testing::Test {
 protected:
  CapportProxyTest()
      : fake_transport_(std::make_shared<brillo::http::fake::Transport>()),
        proxy_(CapportProxy::Create(
            &metrics_, kInterfaceName, kApiUrl, kDnsServers, fake_transport_)) {
  }

  void AddJSONReplyHandler(std::string_view json_str) {
    fake_transport_->AddSimpleReplyHandler(
        kApiUrl.ToString(), brillo::http::request_type::kGet,
        brillo::http::status_code::Ok, std::string(json_str),
        "application/captive+json");
  }

  void IgnoreUninterstedBoolMetric(std::string_view metric_name) {
    EXPECT_CALL(metrics_, SendBoolToUMA(std::string(metric_name), _))
        .Times(AtMost(1));
  }

  MockCapportClient client_;
  MockMetrics metrics_;
  std::shared_ptr<brillo::http::fake::Transport> fake_transport_;
  std::unique_ptr<CapportProxy> proxy_;
};

TEST_F(CapportProxyTest, SendRequest) {
  // Verify if SendRequest() sends the expected HTTP request.
  const std::vector<std::pair<std::string, std::string>> kHeaders = {
      {"Accept", "application/captive+json"}};
  auto mock_transport = std::make_shared<brillo::http::MockTransport>();
  EXPECT_CALL(*mock_transport, SetInterface(kInterfaceName));
  EXPECT_CALL(*mock_transport,
              SetDnsServers(ElementsAre(kDnsServers[0].ToString(),
                                        kDnsServers[1].ToString())));
  EXPECT_CALL(*mock_transport,
              UseCustomCertificate(brillo::http::Transport::Certificate::kNss));
  EXPECT_CALL(
      *mock_transport,
      CreateConnection(kApiUrl.ToString(), brillo::http::request_type::kGet,
                       kHeaders, _, _, _));

  proxy_ = CapportProxy::Create(&metrics_, kInterfaceName, kApiUrl, kDnsServers,
                                mock_transport);
  EXPECT_NE(proxy_, nullptr);

  proxy_->SendRequest(base::DoNothing());
}

TEST_F(CapportProxyTest, SendRequestSuccess) {
  const CapportStatus status{
      .is_captive = true,
      .user_portal_url =
          net_base::HttpUrl::CreateFromString(kUserPortalUrl).value(),
      .venue_info_url = std::nullopt,
      .can_extend_session = std::nullopt,
      .seconds_remaining = std::nullopt,
      .bytes_remaining = std::nullopt,
  };

  AddJSONReplyHandler(R"({
   "captive": true,
   "user-portal-url": "https://example.org/portal.html"
})");

  // When the HTTP server replies a valid JSON string, the client should get
  // the valid status via callback.
  EXPECT_CALL(client_, OnStatusReceived(Eq(status))).Times(2);
  proxy_->SendRequest(base::BindOnce(&MockCapportClient::OnStatusReceived,
                                     base::Unretained(&client_)));
  proxy_->SendRequest(base::BindOnce(&MockCapportClient::OnStatusReceived,
                                     base::Unretained(&client_)));
}

TEST_F(CapportProxyTest, SendRequestFail) {
  fake_transport_->AddSimpleReplyHandler(
      kApiUrl.ToString(), brillo::http::request_type::kGet,
      brillo::http::status_code::Ok, "Invalid JSON string",
      "application/captive+json");

  // When the HTTP server replies an invalid JSON string, the client should get
  // std::nullopt via callback.
  EXPECT_CALL(client_, OnStatusReceived(Eq(std::nullopt)));
  proxy_->SendRequest(base::BindOnce(&MockCapportClient::OnStatusReceived,
                                     base::Unretained(&client_)));
}

TEST_F(CapportProxyTest, SendRequestAndStop) {
  fake_transport_->SetAsyncMode(true);
  fake_transport_->AddSimpleReplyHandler(
      kApiUrl.ToString(), brillo::http::request_type::kGet,
      brillo::http::status_code::Ok, "Invalid JSON string",
      "application/captive+json");

  // When stopping proxy before the transport is done, the client should not get
  // callback.
  EXPECT_CALL(client_, OnStatusReceived).Times(0);

  proxy_->SendRequest(base::BindOnce(&MockCapportClient::OnStatusReceived,
                                     base::Unretained(&client_)));
  proxy_->Stop();
  fake_transport_->HandleAllAsyncRequests();  // Simulate the transport is done.
}

TEST_F(CapportProxyTest, SendMetricsContainVenueInfoUrl) {
  // Send the metric only once even we receive the status twice.
  EXPECT_CALL(metrics_,
              SendBoolToUMA(Metrics::kMetricCapportContainsVenueInfoUrl, true))
      .Times(1);
  IgnoreUninterstedBoolMetric(Metrics::kMetricCapportContainsSecondsRemaining);

  AddJSONReplyHandler(R"({
   "captive": false,
   "user-portal-url": "https://example.org/portal.html",
   "venue-info-url": "https://flight.example.com/entertainment"
})");

  proxy_->SendRequest(base::BindOnce(&MockCapportClient::OnStatusReceived,
                                     base::Unretained(&client_)));
  proxy_->SendRequest(base::BindOnce(&MockCapportClient::OnStatusReceived,
                                     base::Unretained(&client_)));
}

TEST_F(CapportProxyTest, SendMetricsNotContainVenueInfoUrl) {
  // If there is no venue info URL when the portal is open, then the CAPPORT
  // server doesn't contain the venue info URL.
  EXPECT_CALL(metrics_,
              SendBoolToUMA(Metrics::kMetricCapportContainsVenueInfoUrl, false))
      .Times(1);
  IgnoreUninterstedBoolMetric(Metrics::kMetricCapportContainsSecondsRemaining);

  AddJSONReplyHandler(R"({
   "captive": false,
   "user-portal-url": "https://example.org/portal.html"
})");

  proxy_->SendRequest(base::BindOnce(&MockCapportClient::OnStatusReceived,
                                     base::Unretained(&client_)));
  proxy_->SendRequest(base::BindOnce(&MockCapportClient::OnStatusReceived,
                                     base::Unretained(&client_)));
}

TEST_F(CapportProxyTest, VenueInfoUrlInSecondRound) {
  // If the first status doesn't contain the venue info URL but the second
  // status contains it, then we treat the CAPPORT server contains the venue
  // info URL.
  EXPECT_CALL(metrics_,
              SendBoolToUMA(Metrics::kMetricCapportContainsVenueInfoUrl, true))
      .Times(1);
  IgnoreUninterstedBoolMetric(Metrics::kMetricCapportContainsSecondsRemaining);

  AddJSONReplyHandler(R"({
   "captive": false,
   "user-portal-url": "https://example.org/portal.html"
})");
  proxy_->SendRequest(base::BindOnce(&MockCapportClient::OnStatusReceived,
                                     base::Unretained(&client_)));

  AddJSONReplyHandler(R"({
   "captive": false,
   "user-portal-url": "https://example.org/portal.html",
   "venue-info-url": "https://flight.example.com/entertainment"
})");
  proxy_->SendRequest(base::BindOnce(&MockCapportClient::OnStatusReceived,
                                     base::Unretained(&client_)));
}

TEST_F(CapportProxyTest, DoesNotSendMetricsContainVenueInfoUrl) {
  // The venue info URL might be sent after the portal is open. So we cannot
  // determine if the CAPPORT server contains venue info URL when the portal is
  // still closed.
  EXPECT_CALL(metrics_,
              SendBoolToUMA(Metrics::kMetricCapportContainsVenueInfoUrl, _))
      .Times(0);

  AddJSONReplyHandler(R"({
   "captive": true,
   "user-portal-url": "https://example.org/portal.html"
})");

  proxy_->SendRequest(base::BindOnce(&MockCapportClient::OnStatusReceived,
                                     base::Unretained(&client_)));
}

TEST_F(CapportProxyTest, SendMetricsContainSecondsRemaining) {
  // Send the metric only once even we receive the status twice.
  EXPECT_CALL(
      metrics_,
      SendBoolToUMA(Metrics::kMetricCapportContainsSecondsRemaining, true))
      .Times(1);
  EXPECT_CALL(metrics_,
              SendToUMA(Metrics::kMetricCapportMaxSecondsRemaining, 326))
      .Times(1);
  IgnoreUninterstedBoolMetric(Metrics::kMetricCapportContainsVenueInfoUrl);

  AddJSONReplyHandler(R"({
   "captive": false,
   "seconds-remaining": 326
})");

  proxy_->SendRequest(base::BindOnce(&MockCapportClient::OnStatusReceived,
                                     base::Unretained(&client_)));
  proxy_->SendRequest(base::BindOnce(&MockCapportClient::OnStatusReceived,
                                     base::Unretained(&client_)));
}

TEST_F(CapportProxyTest, SendMetricsNotContainSecondsRemaining) {
  // If there is no seconds_remaining when the portal is open, then the CAPPORT
  // server doesn't contain the seconds_remaining field.
  EXPECT_CALL(
      metrics_,
      SendBoolToUMA(Metrics::kMetricCapportContainsSecondsRemaining, false))
      .Times(1);
  IgnoreUninterstedBoolMetric(Metrics::kMetricCapportContainsVenueInfoUrl);

  AddJSONReplyHandler(R"({
   "captive": false
})");
  proxy_->SendRequest(base::BindOnce(&MockCapportClient::OnStatusReceived,
                                     base::Unretained(&client_)));
  proxy_->SendRequest(base::BindOnce(&MockCapportClient::OnStatusReceived,
                                     base::Unretained(&client_)));
}

TEST_F(CapportProxyTest, SecondsRemainingInSecondRound) {
  // If the first status doesn't contain the seconds_remaining but the second
  // status contains it, then we consider the CAPPORT server contains the
  // seconds_remaining field.
  EXPECT_CALL(
      metrics_,
      SendBoolToUMA(Metrics::kMetricCapportContainsSecondsRemaining, true))
      .Times(1);
  EXPECT_CALL(metrics_,
              SendToUMA(Metrics::kMetricCapportMaxSecondsRemaining, 326))
      .Times(1);
  IgnoreUninterstedBoolMetric(Metrics::kMetricCapportContainsVenueInfoUrl);

  AddJSONReplyHandler(R"({
   "captive": false
})");
  proxy_->SendRequest(base::BindOnce(&MockCapportClient::OnStatusReceived,
                                     base::Unretained(&client_)));

  AddJSONReplyHandler(R"({
   "captive": false,
   "seconds-remaining": 310
})");
  proxy_->SendRequest(base::BindOnce(&MockCapportClient::OnStatusReceived,
                                     base::Unretained(&client_)));

  AddJSONReplyHandler(R"({
   "captive": false,
   "seconds-remaining": 326
})");
  proxy_->SendRequest(base::BindOnce(&MockCapportClient::OnStatusReceived,
                                     base::Unretained(&client_)));

  AddJSONReplyHandler(R"({
   "captive": false
})");
  proxy_->SendRequest(base::BindOnce(&MockCapportClient::OnStatusReceived,
                                     base::Unretained(&client_)));
}

TEST_F(CapportProxyTest, DoesNotSendMetricsContainSecondsRemaining) {
  // The seconds_remaining field only exists when the portal is open. So we
  // cannot determine if the CAPPORT server contains the field when the portal
  // is still closed.
  EXPECT_CALL(metrics_,
              SendBoolToUMA(Metrics::kMetricCapportContainsSecondsRemaining, _))
      .Times(0);

  AddJSONReplyHandler(R"({
   "captive": true,
   "user-portal-url": "https://example.org/portal.html"
})");
  proxy_->SendRequest(base::BindOnce(&MockCapportClient::OnStatusReceived,
                                     base::Unretained(&client_)));
}

}  // namespace shill
