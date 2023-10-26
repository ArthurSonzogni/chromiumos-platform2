// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/http_request.h"

#include <netinet/in.h>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <base/functional/bind.h>
#include <base/memory/weak_ptr.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/stringprintf.h>
#include <base/time/time.h>
#include <brillo/http/mock_connection.h>
#include <brillo/http/mock_transport.h>
#include <brillo/mime_utils.h>
#include <brillo/streams/mock_stream.h>
#include <curl/curl.h>
#include <gtest/gtest.h>
#include <net-base/dns_client.h>
#include <net-base/http_url.h>
#include <net-base/ip_address.h>

#include "shill/mock_control.h"
#include "shill/mock_manager.h"
#include "shill/test_event_dispatcher.h"

using ::testing::_;
using ::testing::AtLeast;
using ::testing::DoAll;
using ::testing::Eq;
using ::testing::Invoke;
using ::testing::Mock;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::ReturnRef;
using ::testing::SetArgPointee;
using ::testing::StrEq;
using ::testing::StrictMock;
using ::testing::Test;
using ::testing::Unused;
using ::testing::WithArg;

namespace shill {

namespace {
constexpr std::string_view kTextSiteName = "www.chromium.org";
constexpr std::string_view kTextURL =
    "http://www.chromium.org/path/to/resource";
constexpr std::string_view kIPv4AddressURL = "http://10.1.1.1";
constexpr std::string_view kIPv6AddressURL = "http://[2001:db8::1]/example";
constexpr std::string_view kInterfaceName = "int0";
constexpr std::string_view kLoggingTag = "int0 IPv4 attempt=1";
constexpr net_base::IPAddress kIPv4DNS0(net_base::IPv4Address(8, 8, 8, 8));
constexpr net_base::IPAddress kIPv4DNS1(net_base::IPv4Address(8, 8, 4, 4));
// clang-format off
constexpr net_base::IPAddress kIPv6DNS0(net_base::IPv6Address(0x20, 0x01,
                                                              0x48, 0x60,
                                                              0x48, 0x60,
                                                              0x00, 0x00,
                                                              0x00, 0x00,
                                                              0x00, 0x00,
                                                              0x00, 0x00,
                                                              0x88, 0x88));
constexpr net_base::IPAddress kIPv6DNS1(net_base::IPv6Address(0x20, 0x01,
                                                              0x48, 0x60,
                                                              0x48, 0x60,
                                                              0x00, 0x00,
                                                              0x00, 0x00,
                                                              0x00, 0x00,
                                                              0x00, 0x00,
                                                              0x88, 0x44));
// clang-format on
}  // namespace

MATCHER_P(ByteStringMatches, byte_string, "") {
  return byte_string.Equals(arg);
}

MATCHER_P(CallbackEq, callback, "") {
  return arg.Equals(callback);
}

// The fake client doesn't need to do anything. WeakPtrFactory is for querying
// whether the object is still valid in the test.
class FakeDNSClient : public net_base::DNSClient {
 public:
  FakeDNSClient(std::string_view hostname,
                std::optional<net_base::IPAddress> dns)
      : hostname_(hostname), dns_(dns) {}

  base::WeakPtr<FakeDNSClient> AsWeakPtr() {
    return weak_factory_.GetWeakPtr();
  }

  std::string_view hostname() { return hostname_; }
  std::optional<net_base::IPAddress> dns() { return dns_; }

 private:
  std::string hostname_;
  std::optional<net_base::IPAddress> dns_;
  base::WeakPtrFactory<FakeDNSClient> weak_factory_{this};
};

class FakeDNSClientFactory : public net_base::DNSClientFactory {
 public:
  using Callbacks = std::vector<net_base::DNSClient::Callback>;

  FakeDNSClientFactory() {
    ON_CALL(*this, Resolve)
        .WillByDefault([this](
                           net_base::IPFamily family, std::string_view hostname,
                           net_base::DNSClient::CallbackWithDuration callback,
                           const net_base::DNSClient::Options& options,
                           net_base::AresInterface* ares_interface) {
          callbacks_.emplace_back(std::move(callback));
          auto ret =
              std::make_unique<FakeDNSClient>(hostname, options.name_server);
          clients_.push_back(ret->AsWeakPtr());
          return ret;
        });
  }

  MOCK_METHOD(std::unique_ptr<net_base::DNSClient>,
              Resolve,
              (net_base::IPFamily family,
               std::string_view hostname,
               net_base::DNSClient::CallbackWithDuration callback,
               const net_base::DNSClient::Options& options,
               net_base::AresInterface* ares_interface),
              (override));

  void TriggerCallback(const net_base::DNSClient::Result& result) {
    std::move(callbacks_.back()).Run(base::Milliseconds(100), result);
    callbacks_.pop_back();
  }

  std::vector<base::WeakPtr<FakeDNSClient>> GetWeakPtrsToExistingClients()
      const {
    return clients_;
  }

 private:
  std::vector<net_base::DNSClient::CallbackWithDuration> callbacks_;
  std::vector<base::WeakPtr<FakeDNSClient>> clients_;
};

class HttpRequestTest : public Test {
 public:
  HttpRequestTest()
      : transport_(std::make_shared<brillo::http::MockTransport>()),
        brillo_connection_(
            std::make_shared<brillo::http::MockConnection>(transport_)),
        manager_(&control_, &dispatcher_, nullptr) {}

 protected:
  class CallbackTarget {
   public:
    MOCK_METHOD(void,
                RequestSuccessCallTarget,
                (std::shared_ptr<brillo::http::Response>));
    MOCK_METHOD(void, RequestErrorCallTarget, (HttpRequest::Error));

    base::OnceCallback<void(std::shared_ptr<brillo::http::Response>)>
    request_success_callback() {
      return base::BindOnce(&CallbackTarget::RequestSuccessCallTarget,
                            base::Unretained(this));
    }

    base::OnceCallback<void(HttpRequest::Error)> request_error_callback() {
      return base::BindOnce(&CallbackTarget::RequestErrorCallTarget,
                            base::Unretained(this));
    }
  };

  HttpRequest* request() { return request_.get(); }
  std::shared_ptr<brillo::http::MockTransport> transport() {
    return transport_;
  }

  // Expectations
  void ExpectStopped() {
    EXPECT_FALSE(request_->is_running());
    if (dns_client_factory_) {
      for (const auto& client :
           dns_client_factory_->GetWeakPtrsToExistingClients()) {
        EXPECT_TRUE(client.WasInvalidated());
      }
    }
  }
  void VerifyDNSRequests(std::string_view hostname,
                         const std::vector<net_base::IPAddress>& dns_list) {
    ASSERT_NE(nullptr, dns_client_factory_);
    auto dns_clients = dns_client_factory_->GetWeakPtrsToExistingClients();
    auto dns_list_copy = dns_list;
    EXPECT_EQ(dns_list_copy.size(), dns_clients.size());
    for (const auto& client : dns_clients) {
      ASSERT_NE(nullptr, client);
      EXPECT_EQ(hostname, client->hostname());
      ASSERT_TRUE(client->dns().has_value());
      auto it =
          std::find(dns_list_copy.begin(), dns_list_copy.end(), *client->dns());
      EXPECT_NE(it, dns_list_copy.end());
      dns_list_copy.erase(it);
    }
    EXPECT_TRUE(dns_list_copy.empty());
  }
  void ExpectRequestErrorCallback(HttpRequest::Error error) {
    EXPECT_CALL(target_, RequestErrorCallTarget(error));
  }
  void InvokeResultVerify(std::shared_ptr<brillo::http::Response> response) {
    EXPECT_CALL(*brillo_connection_, GetResponseStatusCode())
        .WillOnce(Return(brillo::http::status_code::Partial));
    EXPECT_EQ(brillo::http::status_code::Partial, response->GetStatusCode());

    EXPECT_CALL(*brillo_connection_, GetResponseStatusText())
        .WillOnce(Return("Partial completion"));
    EXPECT_EQ("Partial completion", response->GetStatusText());

    EXPECT_CALL(*brillo_connection_,
                GetResponseHeader(brillo::http::response_header::kContentType))
        .WillOnce(Return(brillo::mime::text::kHtml));
    EXPECT_EQ(brillo::mime::text::kHtml, response->GetContentType());

    EXPECT_EQ(expected_response_, response->ExtractDataAsString());
  }
  void ExpectRequestSuccessCallback(const std::string& resp_data) {
    expected_response_ = resp_data;
    EXPECT_CALL(target_, RequestSuccessCallTarget(_))
        .WillOnce(Invoke(this, &HttpRequestTest::InvokeResultVerify));
  }
  void CreateRequest(std::string_view interface_name,
                     net_base::IPFamily ip_family,
                     const std::vector<net_base::IPAddress>& dns_list) {
    dns_client_factory_ = new FakeDNSClientFactory();
    EXPECT_CALL(*transport_, SetInterface(Eq(interface_name)));
    EXPECT_CALL(*transport_, UseCustomCertificate).Times(0);
    request_.reset(new HttpRequest(&dispatcher_, interface_name, ip_family,
                                   dns_list, false, transport_,
                                   base::WrapUnique(dns_client_factory_)));
  }
  void GetDNSResultFailure(net_base::IPAddress dns,
                           net_base::DNSClient::Error error) {
    request_->GetDNSResult(dns, base::Milliseconds(100),
                           base::unexpected(error));
  }
  void GetDNSResultSuccess(net_base::IPAddress dns,
                           const std::vector<net_base::IPAddress>& addresses) {
    request_->GetDNSResult(dns, base::Milliseconds(100), addresses);
  }
  void StartRequest(std::string_view url_string) {
    auto url = net_base::HttpUrl::CreateFromString(url_string);
    ASSERT_TRUE(url.has_value());
    request_->Start(kLoggingTag, *url, {}, target_.request_success_callback(),
                    target_.request_error_callback());
  }
  void ExpectCreateConnection(std::string_view url) {
    EXPECT_CALL(*transport_,
                CreateConnection(Eq(url), brillo::http::request_type::kGet, _,
                                 "", "", _))
        .WillOnce(Return(brillo_connection_));
  }
  void FinishRequestAsyncSuccess(
      brillo::http::SuccessCallback success_callback) {
    auto read_data = [this](void* buffer, Unused, size_t* read,
                            Unused) -> bool {
      memcpy(buffer, resp_data_.data(), resp_data_.size());
      *read = resp_data_.size();
      return true;
    };

    auto mock_stream = std::make_unique<brillo::MockStream>();
    EXPECT_CALL(*mock_stream, ReadBlocking(_, _, _, _))
        .WillOnce(Invoke(read_data))
        .WillOnce(DoAll(SetArgPointee<2>(0), Return(true)));

    EXPECT_CALL(*brillo_connection_, MockExtractDataStream(_))
        .WillOnce(Return(mock_stream.release()));
    auto resp = std::make_unique<brillo::http::Response>(brillo_connection_);
    // request_id_ has not yet been set. Pass -1 to match default value.
    std::move(success_callback).Run(-1, std::move(resp));
  }
  void FinishRequestAsyncFail(brillo::http::ErrorCallback error_callback) {
    brillo::ErrorPtr error;
    brillo::Error::AddTo(&error, FROM_HERE, "curl_easy_error",
                         base::NumberToString(CURLE_COULDNT_CONNECT), "");
    // request_id_ has not yet been set. Pass -1 to match default value.
    std::move(error_callback).Run(-1, error.get());
  }
  void ExpectFinishRequestAsyncSuccess(const std::string& resp_data) {
    resp_data_ = resp_data;
    EXPECT_CALL(*brillo_connection_, FinishRequestAsync(_, _))
        .WillOnce(WithArg<0>([this](auto callback) {
          FinishRequestAsyncSuccess(std::move(callback));
          return 0;
        }));
  }
  void ExpectFinishRequestAsyncFail() {
    EXPECT_CALL(*brillo_connection_, FinishRequestAsync(_, _))
        .WillOnce(WithArg<1>([this](auto callback) {
          FinishRequestAsyncFail(std::move(callback));
          return 0;
        }));
  }

 protected:
  FakeDNSClientFactory* dns_client_factory_;
  std::shared_ptr<brillo::http::MockTransport> transport_;
  std::shared_ptr<brillo::http::MockConnection> brillo_connection_;
  EventDispatcherForTest dispatcher_;
  MockControl control_;
  MockManager manager_;
  std::unique_ptr<HttpRequest> request_;
  StrictMock<CallbackTarget> target_;
  std::string expected_response_;
  std::string resp_data_;
};

TEST_F(HttpRequestTest, Constructor) {
  CreateRequest(kInterfaceName, net_base::IPFamily::kIPv4,
                {kIPv4DNS0, kIPv4DNS1});
  ExpectStopped();
}

TEST_F(HttpRequestTest, UseCustomCertificate) {
  auto transport = std::make_shared<brillo::http::MockTransport>();
  EXPECT_CALL(*transport,
              UseCustomCertificate(brillo::http::Transport::Certificate::kNss));

  std::vector<net_base::IPAddress> dns_list = {kIPv4DNS0, kIPv4DNS1};
  auto request = std::make_unique<HttpRequest>(
      &dispatcher_, "wlan0", net_base::IPFamily::kIPv4, dns_list,
      /*allow_non_google_https=*/true, transport,
      std::make_unique<FakeDNSClientFactory>());
}

TEST_F(HttpRequestTest, IPv4NumericRequestSuccess) {
  CreateRequest(kInterfaceName, net_base::IPFamily::kIPv4,
                {kIPv4DNS0, kIPv4DNS1});

  const std::string resp = "Sample response.";
  ExpectRequestSuccessCallback(resp);
  EXPECT_CALL(*transport(), ResolveHostToIp).Times(0);
  ExpectCreateConnection(kIPv4AddressURL);
  ExpectFinishRequestAsyncSuccess(resp);

  StartRequest(kIPv4AddressURL);
  ExpectStopped();
}

TEST_F(HttpRequestTest, IPv4NumericRequestFail) {
  CreateRequest(kInterfaceName, net_base::IPFamily::kIPv4,
                {kIPv4DNS0, kIPv4DNS1});

  ExpectRequestErrorCallback(HttpRequest::Error::kConnectionFailure);
  ExpectCreateConnection(kIPv4AddressURL);
  ExpectFinishRequestAsyncFail();

  StartRequest(kIPv4AddressURL);
  ExpectStopped();
}

// TODO(cros-networking@): Re-enable this test when HttpUrl supports parsing
// IPv6 address hosts.
TEST_F(HttpRequestTest, DISABLED_IPv6NumericRequestSuccess) {
  CreateRequest(kInterfaceName, net_base::IPFamily::kIPv6,
                {kIPv6DNS0, kIPv6DNS1});

  const std::string resp = "Sample response.";
  ExpectRequestSuccessCallback(resp);
  EXPECT_CALL(*transport(), ResolveHostToIp).Times(0);
  ExpectCreateConnection(kIPv6AddressURL);
  ExpectFinishRequestAsyncSuccess(resp);

  StartRequest(kIPv6AddressURL);
  ExpectStopped();
}

// TODO(cros-networking@): Re-enable this test when HttpUrl supports parsing
// IPv6 address hosts.
TEST_F(HttpRequestTest, DISABLED_IPv6NumericRequestFail) {
  CreateRequest(kInterfaceName, net_base::IPFamily::kIPv6,
                {kIPv6DNS0, kIPv6DNS1});

  ExpectRequestErrorCallback(HttpRequest::Error::kConnectionFailure);
  ExpectCreateConnection(kIPv6AddressURL);
  ExpectFinishRequestAsyncFail();

  StartRequest(kIPv6AddressURL);
  ExpectStopped();
}

TEST_F(HttpRequestTest, IPv4TextRequestSuccess) {
  CreateRequest(kInterfaceName, net_base::IPFamily::kIPv4,
                {kIPv4DNS0, kIPv4DNS1});

  StartRequest(kTextURL);
  VerifyDNSRequests(kTextSiteName, {kIPv4DNS0, kIPv4DNS1});

  const std::string resp = "Sample response.";
  ExpectRequestSuccessCallback(resp);
  auto url = *net_base::HttpUrl::CreateFromString(kTextURL);
  EXPECT_CALL(*transport(), ResolveHostToIp(url.host(), url.port(),
                                            "10.1.1.1,10.1.1.2,10.1.1.3"));
  ExpectCreateConnection(kTextURL);
  ExpectFinishRequestAsyncSuccess(resp);

  GetDNSResultSuccess(
      kIPv4DNS0, {
                     net_base::IPAddress(net_base::IPv4Address(10, 1, 1, 1)),
                     net_base::IPAddress(net_base::IPv4Address(10, 1, 1, 2)),
                     net_base::IPAddress(net_base::IPv4Address(10, 1, 1, 3)),
                 });

  ExpectStopped();
}

TEST_F(HttpRequestTest, IPv6TextRequestSuccess) {
  CreateRequest(kInterfaceName, net_base::IPFamily::kIPv6,
                {kIPv6DNS0, kIPv6DNS1});

  StartRequest(kTextURL);
  VerifyDNSRequests(kTextSiteName, {kIPv6DNS0, kIPv6DNS1});

  const std::string resp = "Sample response.";
  ExpectRequestSuccessCallback(resp);
  auto url = *net_base::HttpUrl::CreateFromString(kTextURL);
  EXPECT_CALL(*transport(),
              ResolveHostToIp(url.host(), url.port(), "2001:db8::1"));
  ExpectCreateConnection(kTextURL);
  ExpectFinishRequestAsyncSuccess(resp);

  const auto addr = *net_base::IPAddress::CreateFromString("2001:db8::1");
  GetDNSResultSuccess(kIPv6DNS0, {addr});

  ExpectStopped();
}

// multiple addresses

TEST_F(HttpRequestTest, IPv4FailDNSFailure) {
  CreateRequest(kInterfaceName, net_base::IPFamily::kIPv4,
                {kIPv4DNS0, kIPv4DNS1});
  StartRequest(kTextURL);
  VerifyDNSRequests(kTextSiteName, {kIPv4DNS0, kIPv4DNS1});
  ExpectRequestErrorCallback(HttpRequest::Error::kDNSFailure);
  GetDNSResultFailure(kIPv4DNS0, net_base::DNSClient::Error::kNoData);
  GetDNSResultFailure(kIPv4DNS1, net_base::DNSClient::Error::kNoData);
  ExpectStopped();
}

TEST_F(HttpRequestTest, IPv4FailDNSTimeout) {
  CreateRequest(kInterfaceName, net_base::IPFamily::kIPv4,
                {kIPv4DNS0, kIPv4DNS1});
  StartRequest(kTextURL);
  VerifyDNSRequests(kTextSiteName, {kIPv4DNS0, kIPv4DNS1});
  ExpectRequestErrorCallback(HttpRequest::Error::kDNSTimeout);
  GetDNSResultFailure(kIPv4DNS0, net_base::DNSClient::Error::kTimedOut);
  GetDNSResultFailure(kIPv4DNS1, net_base::DNSClient::Error::kTimedOut);
  ExpectStopped();
}

TEST_F(HttpRequestTest, IPv6FailDNSFailure) {
  CreateRequest(kInterfaceName, net_base::IPFamily::kIPv6,
                {kIPv6DNS0, kIPv6DNS1});
  StartRequest(kTextURL);
  VerifyDNSRequests(kTextSiteName, {kIPv6DNS0, kIPv6DNS1});
  ExpectRequestErrorCallback(HttpRequest::Error::kDNSFailure);
  GetDNSResultFailure(kIPv6DNS0, net_base::DNSClient::Error::kNoData);
  GetDNSResultFailure(kIPv6DNS1, net_base::DNSClient::Error::kNoData);
  ExpectStopped();
}

TEST_F(HttpRequestTest, IPv6FailDNSTimeout) {
  CreateRequest(kInterfaceName, net_base::IPFamily::kIPv6,
                {kIPv6DNS0, kIPv6DNS1});
  StartRequest(kTextURL);
  VerifyDNSRequests(kTextSiteName, {kIPv6DNS0, kIPv6DNS1});
  ExpectRequestErrorCallback(HttpRequest::Error::kDNSTimeout);
  GetDNSResultFailure(kIPv6DNS0, net_base::DNSClient::Error::kTimedOut);
  GetDNSResultFailure(kIPv6DNS1, net_base::DNSClient::Error::kTimedOut);
  ExpectStopped();
}

TEST_F(HttpRequestTest, IPv4TextRequestSuccessAfterSomeDNSError) {
  CreateRequest(kInterfaceName, net_base::IPFamily::kIPv4,
                {kIPv6DNS0, kIPv6DNS1});

  StartRequest(kTextURL);
  VerifyDNSRequests(kTextSiteName, {kIPv6DNS0, kIPv6DNS1});

  EXPECT_CALL(*transport(), ResolveHostToIp).Times(0);
  GetDNSResultFailure(kIPv4DNS0, net_base::DNSClient::Error::kTimedOut);
  EXPECT_TRUE(request_->is_running());
  Mock::VerifyAndClearExpectations(transport().get());

  const std::string resp = "Sample response.";
  ExpectRequestSuccessCallback(resp);
  auto url = *net_base::HttpUrl::CreateFromString(kTextURL);
  EXPECT_CALL(*transport(),
              ResolveHostToIp(url.host(), url.port(), "10.1.1.1"));
  ExpectCreateConnection(kTextURL);
  ExpectFinishRequestAsyncSuccess(resp);

  GetDNSResultSuccess(
      kIPv4DNS1, {net_base::IPAddress(net_base::IPv4Address(10, 1, 1, 1))});

  ExpectStopped();
}

}  // namespace shill
