// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/network/http_request.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <base/functional/bind.h>
#include <base/memory/weak_ptr.h>
#include <base/strings/stringprintf.h>
#include <base/time/time.h>
#include <base/types/expected.h>
#include <brillo/http/http_transport_error.h>
#include <brillo/http/mock_connection.h>
#include <brillo/http/mock_transport.h>
#include <brillo/mime_utils.h>
#include <brillo/streams/mock_stream.h>
#include <chromeos/net-base/dns_client.h>
#include <chromeos/net-base/http_url.h>
#include <chromeos/net-base/ip_address.h>
#include <gtest/gtest.h>

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
// Sentinel for `brillo::http::RequestID` when no real request has been issued.
constexpr brillo::http::RequestID kNoRequestId = -1;
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
    MOCK_METHOD(void, RequestCallback, (HttpRequest::Result result));

    base::OnceCallback<void(HttpRequest::Result result)> callback() {
      return base::BindOnce(&CallbackTarget::RequestCallback,
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
  void ExpectRequestErrorCallback(brillo::http::TransportError error) {
    EXPECT_CALL(target_, RequestCallback(Eq(base::unexpected(error))));
  }
  void InvokeResultVerifySuccess(HttpRequest::Result result) {
    ASSERT_TRUE(result.has_value());
    std::unique_ptr<brillo::http::Response> response = std::move(*result);
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
    EXPECT_CALL(target_, RequestCallback)
        .WillOnce(Invoke(this, &HttpRequestTest::InvokeResultVerifySuccess));
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
  void StartRequest(HttpRequest::Method method,
                    std::string_view url_string,
                    std::optional<base::TimeDelta> timeout = std::nullopt) {
    auto url = net_base::HttpUrl::CreateFromString(url_string);
    ASSERT_TRUE(url.has_value());
    request_->Start(method, kLoggingTag, *url, {}, target_.callback(),
                    /*timeout=*/timeout);
  }
  void StartRequestWithRetry(std::string_view url_string,
                             HttpRequest::RetryPolicy policy) {
    auto url = net_base::HttpUrl::CreateFromString(url_string);
    ASSERT_TRUE(url.has_value());
    request_->Start(HttpRequest::Method::kGet, kLoggingTag, *url, {},
                    target_.callback(), /*timeout=*/std::nullopt,
                    std::move(policy));
  }
  // Configures `FinishRequestAsync` to fail `fail_count` times with
  // `transport_error`, then succeed on the next call with `resp_data`.
  void SetupFinishRequestFailThenSuccess(
      size_t fail_count,
      brillo::http::TransportError transport_error,
      const std::string& resp_data) {
    resp_data_ = resp_data;
    auto calls = std::make_shared<size_t>(0);
    constexpr size_t kSuccessAttempt = 1;
    EXPECT_CALL(*brillo_connection_, FinishRequestAsync(_, _))
        .Times(fail_count + kSuccessAttempt)
        .WillRepeatedly([this, calls, fail_count, transport_error](
                            brillo::http::SuccessCallback success_callback,
                            brillo::http::ErrorCallback error_callback) {
          (*calls)++;
          if (*calls <= fail_count) {
            brillo::ErrorPtr error;
            brillo::http::AddTransportError(&error, FROM_HERE, transport_error,
                                            "");
            std::move(error_callback).Run(kNoRequestId, error.get());
          } else {
            FinishRequestAsyncSuccess(std::move(success_callback));
          }
          return 0;
        });
  }
  // Configures `FinishRequestAsync` to fail once with `transport_error`.
  void ExpectFinishRequestAsyncTransportError(
      brillo::http::TransportError transport_error) {
    EXPECT_CALL(*brillo_connection_, FinishRequestAsync(_, _))
        .WillOnce(WithArg<1>([transport_error](auto callback) {
          brillo::ErrorPtr error;
          brillo::http::AddTransportError(&error, FROM_HERE, transport_error,
                                          "");
          std::move(callback).Run(kNoRequestId, error.get());
          return 0;
        }));
  }
  void ExpectCreateConnection(std::string_view url, const char* method) {
    EXPECT_CALL(*transport_, CreateConnection(Eq(url), method, _, "", "", _))
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
    std::move(success_callback).Run(kNoRequestId, std::move(resp));
  }
  void FinishRequestAsyncFail(brillo::http::ErrorCallback error_callback) {
    brillo::ErrorPtr error;
    brillo::http::AddTransportError(
        &error, FROM_HERE, brillo::http::TransportError::kConnectionFailure,
        "");
    // request_id_ has not yet been set. Pass kNoRequestId to match default.
    std::move(error_callback).Run(kNoRequestId, error.get());
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
  ExpectCreateConnection(kIPv4AddressURL, brillo::http::request_type::kGet);
  ExpectFinishRequestAsyncSuccess(resp);

  StartRequest(HttpRequest::Method::kGet, kIPv4AddressURL);
  ExpectStopped();
}

TEST_F(HttpRequestTest, IPv4NumericRequestFail) {
  CreateRequest(kInterfaceName, net_base::IPFamily::kIPv4,
                {kIPv4DNS0, kIPv4DNS1});

  ExpectRequestErrorCallback(brillo::http::TransportError::kConnectionFailure);
  ExpectCreateConnection(kIPv4AddressURL, brillo::http::request_type::kGet);
  ExpectFinishRequestAsyncFail();

  StartRequest(HttpRequest::Method::kGet, kIPv4AddressURL);
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
  ExpectCreateConnection(kIPv6AddressURL, brillo::http::request_type::kGet);
  ExpectFinishRequestAsyncSuccess(resp);

  StartRequest(HttpRequest::Method::kGet, kIPv6AddressURL);
  ExpectStopped();
}

// TODO(cros-networking@): Re-enable this test when HttpUrl supports parsing
// IPv6 address hosts.
TEST_F(HttpRequestTest, DISABLED_IPv6NumericRequestFail) {
  CreateRequest(kInterfaceName, net_base::IPFamily::kIPv6,
                {kIPv6DNS0, kIPv6DNS1});

  ExpectRequestErrorCallback(brillo::http::TransportError::kConnectionFailure);
  ExpectCreateConnection(kIPv6AddressURL, brillo::http::request_type::kGet);
  ExpectFinishRequestAsyncFail();

  StartRequest(HttpRequest::Method::kGet, kIPv6AddressURL);
  ExpectStopped();
}

TEST_F(HttpRequestTest, IPv4TextRequestSuccess) {
  CreateRequest(kInterfaceName, net_base::IPFamily::kIPv4,
                {kIPv4DNS0, kIPv4DNS1});

  StartRequest(HttpRequest::Method::kGet, kTextURL);
  VerifyDNSRequests(kTextSiteName, {kIPv4DNS0, kIPv4DNS1});

  const std::string resp = "Sample response.";
  ExpectRequestSuccessCallback(resp);
  auto url = *net_base::HttpUrl::CreateFromString(kTextURL);
  EXPECT_CALL(*transport(), ResolveHostToIp(url.host(), url.port(),
                                            "10.1.1.1,10.1.1.2,10.1.1.3"));
  ExpectCreateConnection(kTextURL, brillo::http::request_type::kGet);
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

  StartRequest(HttpRequest::Method::kGet, kTextURL);
  VerifyDNSRequests(kTextSiteName, {kIPv6DNS0, kIPv6DNS1});

  const std::string resp = "Sample response.";
  ExpectRequestSuccessCallback(resp);
  auto url = *net_base::HttpUrl::CreateFromString(kTextURL);
  EXPECT_CALL(*transport(),
              ResolveHostToIp(url.host(), url.port(), "2001:db8::1"));
  ExpectCreateConnection(kTextURL, brillo::http::request_type::kGet);
  ExpectFinishRequestAsyncSuccess(resp);

  const auto addr = *net_base::IPAddress::CreateFromString("2001:db8::1");
  GetDNSResultSuccess(kIPv6DNS0, {addr});

  ExpectStopped();
}

// multiple addresses

TEST_F(HttpRequestTest, IPv4FailDNSFailure) {
  CreateRequest(kInterfaceName, net_base::IPFamily::kIPv4,
                {kIPv4DNS0, kIPv4DNS1});
  StartRequest(HttpRequest::Method::kGet, kTextURL);
  VerifyDNSRequests(kTextSiteName, {kIPv4DNS0, kIPv4DNS1});
  ExpectRequestErrorCallback(brillo::http::TransportError::kDnsFailure);
  GetDNSResultFailure(kIPv4DNS0, net_base::DNSClient::Error::kNoData);
  GetDNSResultFailure(kIPv4DNS1, net_base::DNSClient::Error::kNoData);
  ExpectStopped();
}

TEST_F(HttpRequestTest, IPv4FailDNSTimeout) {
  CreateRequest(kInterfaceName, net_base::IPFamily::kIPv4,
                {kIPv4DNS0, kIPv4DNS1});
  StartRequest(HttpRequest::Method::kGet, kTextURL);
  VerifyDNSRequests(kTextSiteName, {kIPv4DNS0, kIPv4DNS1});
  ExpectRequestErrorCallback(brillo::http::TransportError::kDnsTimeout);
  GetDNSResultFailure(kIPv4DNS0, net_base::DNSClient::Error::kTimedOut);
  GetDNSResultFailure(kIPv4DNS1, net_base::DNSClient::Error::kTimedOut);
  ExpectStopped();
}

TEST_F(HttpRequestTest, IPv6FailDNSFailure) {
  CreateRequest(kInterfaceName, net_base::IPFamily::kIPv6,
                {kIPv6DNS0, kIPv6DNS1});
  StartRequest(HttpRequest::Method::kGet, kTextURL);
  VerifyDNSRequests(kTextSiteName, {kIPv6DNS0, kIPv6DNS1});
  ExpectRequestErrorCallback(brillo::http::TransportError::kDnsFailure);
  GetDNSResultFailure(kIPv6DNS0, net_base::DNSClient::Error::kNoData);
  GetDNSResultFailure(kIPv6DNS1, net_base::DNSClient::Error::kNoData);
  ExpectStopped();
}

TEST_F(HttpRequestTest, IPv6FailDNSTimeout) {
  CreateRequest(kInterfaceName, net_base::IPFamily::kIPv6,
                {kIPv6DNS0, kIPv6DNS1});
  StartRequest(HttpRequest::Method::kGet, kTextURL);
  VerifyDNSRequests(kTextSiteName, {kIPv6DNS0, kIPv6DNS1});
  ExpectRequestErrorCallback(brillo::http::TransportError::kDnsTimeout);
  GetDNSResultFailure(kIPv6DNS0, net_base::DNSClient::Error::kTimedOut);
  GetDNSResultFailure(kIPv6DNS1, net_base::DNSClient::Error::kTimedOut);
  ExpectStopped();
}

TEST_F(HttpRequestTest, IPv4TextRequestSuccessAfterSomeDNSError) {
  CreateRequest(kInterfaceName, net_base::IPFamily::kIPv4,
                {kIPv6DNS0, kIPv6DNS1});

  StartRequest(HttpRequest::Method::kGet, kTextURL);
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
  ExpectCreateConnection(kTextURL, brillo::http::request_type::kGet);
  ExpectFinishRequestAsyncSuccess(resp);

  GetDNSResultSuccess(
      kIPv4DNS1, {net_base::IPAddress(net_base::IPv4Address(10, 1, 1, 1))});

  ExpectStopped();
}

TEST_F(HttpRequestTest, IPv4NumericHeadRequestSuccess) {
  CreateRequest(kInterfaceName, net_base::IPFamily::kIPv4,
                {kIPv4DNS0, kIPv4DNS1});

  const std::string resp = "Sample response.";
  ExpectRequestSuccessCallback(resp);
  EXPECT_CALL(*transport(), ResolveHostToIp).Times(0);
  ExpectCreateConnection(kIPv4AddressURL, brillo::http::request_type::kHead);
  ExpectFinishRequestAsyncSuccess(resp);

  StartRequest(HttpRequest::Method::kHead, kIPv4AddressURL);
  ExpectStopped();
}

TEST_F(HttpRequestTest, IPv4NumericHeadRequestFail) {
  CreateRequest(kInterfaceName, net_base::IPFamily::kIPv4,
                {kIPv4DNS0, kIPv4DNS1});

  ExpectRequestErrorCallback(brillo::http::TransportError::kConnectionFailure);
  ExpectCreateConnection(kIPv4AddressURL, brillo::http::request_type::kHead);
  ExpectFinishRequestAsyncFail();

  StartRequest(HttpRequest::Method::kHead, kIPv4AddressURL);
  ExpectStopped();
}

TEST_F(HttpRequestTest, IPv4TextHeadRequestSuccess) {
  CreateRequest(kInterfaceName, net_base::IPFamily::kIPv4,
                {kIPv4DNS0, kIPv4DNS1});

  StartRequest(HttpRequest::Method::kHead, kTextURL);
  VerifyDNSRequests(kTextSiteName, {kIPv4DNS0, kIPv4DNS1});

  const std::string resp = "Sample response.";
  ExpectRequestSuccessCallback(resp);
  auto url = *net_base::HttpUrl::CreateFromString(kTextURL);
  EXPECT_CALL(*transport(), ResolveHostToIp(url.host(), url.port(),
                                            "10.1.1.1,10.1.1.2,10.1.1.3"));
  ExpectCreateConnection(kTextURL, brillo::http::request_type::kHead);
  ExpectFinishRequestAsyncSuccess(resp);

  GetDNSResultSuccess(
      kIPv4DNS0, {
                     net_base::IPAddress(net_base::IPv4Address(10, 1, 1, 1)),
                     net_base::IPAddress(net_base::IPv4Address(10, 1, 1, 2)),
                     net_base::IPAddress(net_base::IPv4Address(10, 1, 1, 3)),
                 });

  ExpectStopped();
}

TEST_F(HttpRequestTest, DefaultTimeoutIsUsedWhenNullopt) {
  CreateRequest(kInterfaceName, net_base::IPFamily::kIPv4,
                {kIPv4DNS0, kIPv4DNS1});

  EXPECT_CALL(*transport(), SetDefaultTimeout(base::Seconds(10)));

  const std::string resp = "Sample response.";
  ExpectRequestSuccessCallback(resp);
  EXPECT_CALL(*transport(), ResolveHostToIp).Times(0);
  ExpectCreateConnection(kIPv4AddressURL, brillo::http::request_type::kGet);
  ExpectFinishRequestAsyncSuccess(resp);

  StartRequest(HttpRequest::Method::kGet, kIPv4AddressURL);
  ExpectStopped();
}

TEST_F(HttpRequestTest, ZeroTimeoutUsesDefault) {
  CreateRequest(kInterfaceName, net_base::IPFamily::kIPv4,
                {kIPv4DNS0, kIPv4DNS1});

  EXPECT_CALL(*transport(), SetDefaultTimeout(base::Seconds(10)));

  const std::string resp = "Sample response.";
  ExpectRequestSuccessCallback(resp);
  EXPECT_CALL(*transport(), ResolveHostToIp).Times(0);
  ExpectCreateConnection(kIPv4AddressURL, brillo::http::request_type::kGet);
  ExpectFinishRequestAsyncSuccess(resp);

  StartRequest(HttpRequest::Method::kGet, kIPv4AddressURL, base::TimeDelta());
  ExpectStopped();
}

TEST_F(HttpRequestTest, NegativeTimeoutUsesDefault) {
  CreateRequest(kInterfaceName, net_base::IPFamily::kIPv4,
                {kIPv4DNS0, kIPv4DNS1});

  EXPECT_CALL(*transport(), SetDefaultTimeout(base::Seconds(10)));

  const std::string resp = "Sample response.";
  ExpectRequestSuccessCallback(resp);
  EXPECT_CALL(*transport(), ResolveHostToIp).Times(0);
  ExpectCreateConnection(kIPv4AddressURL, brillo::http::request_type::kGet);
  ExpectFinishRequestAsyncSuccess(resp);

  StartRequest(HttpRequest::Method::kGet, kIPv4AddressURL, base::Seconds(-1));
  ExpectStopped();
}

TEST_F(HttpRequestTest, CustomTimeoutIsApplied) {
  CreateRequest(kInterfaceName, net_base::IPFamily::kIPv4,
                {kIPv4DNS0, kIPv4DNS1});

  constexpr auto custom_timeout = base::Seconds(30);
  EXPECT_CALL(*transport(), SetDefaultTimeout(custom_timeout));

  const std::string resp = "Sample response.";
  ExpectRequestSuccessCallback(resp);
  EXPECT_CALL(*transport(), ResolveHostToIp).Times(0);
  ExpectCreateConnection(kIPv4AddressURL, brillo::http::request_type::kGet);
  ExpectFinishRequestAsyncSuccess(resp);

  StartRequest(HttpRequest::Method::kGet, kIPv4AddressURL, custom_timeout);
  ExpectStopped();
}

TEST_F(HttpRequestTest, NoRetryOnNonRetryableError) {
  CreateRequest(kInterfaceName, net_base::IPFamily::kIPv4,
                {kIPv4DNS0, kIPv4DNS1});

  HttpRequest::RetryPolicy policy;
  ExpectRequestErrorCallback(brillo::http::TransportError::kTLSFailure);
  ExpectCreateConnection(kIPv4AddressURL, brillo::http::request_type::kGet);
  ExpectFinishRequestAsyncTransportError(
      brillo::http::TransportError::kTLSFailure);

  StartRequestWithRetry(kIPv4AddressURL, policy);

  // Pump event loop to ensure no async retry was posted.
  dispatcher_.task_environment().RunUntilIdle();
  ExpectStopped();
}

TEST_F(HttpRequestTest, RetryOnIOError) {
  CreateRequest(kInterfaceName, net_base::IPFamily::kIPv4,
                {kIPv4DNS0, kIPv4DNS1});

  const std::string resp = "Retry success.";
  HttpRequest::RetryPolicy policy;
  ExpectRequestSuccessCallback(resp);
  EXPECT_CALL(*transport(),
              CreateConnection(Eq(kIPv4AddressURL),
                               brillo::http::request_type::kGet, _, "", "", _))
      .WillOnce(Return(brillo_connection_))
      .WillOnce(Return(brillo_connection_));
  SetupFinishRequestFailThenSuccess(1, brillo::http::TransportError::kIOError,
                                    resp);

  StartRequestWithRetry(kIPv4AddressURL, policy);

  // The retry is posted asynchronously. Pump the event loop to fire it.
  dispatcher_.task_environment().RunUntilIdle();
  ExpectStopped();
}

TEST_F(HttpRequestTest, RetryOnDNSTimeout) {
  CreateRequest(kInterfaceName, net_base::IPFamily::kIPv4,
                {kIPv4DNS0, kIPv4DNS1});

  HttpRequest::RetryPolicy policy;

  // Expect Resolve() called 4 times: 2 DNS servers x 2 attempts.
  EXPECT_CALL(*dns_client_factory_, Resolve).Times(4);

  auto url = *net_base::HttpUrl::CreateFromString(kTextURL);
  request_->Start(HttpRequest::Method::kGet, kLoggingTag, url, {},
                  target_.callback(), /*timeout=*/std::nullopt,
                  std::move(policy));

  // First attempt: both DNS servers time out -> triggers retry.
  GetDNSResultFailure(kIPv4DNS0, net_base::DNSClient::Error::kTimedOut);
  GetDNSResultFailure(kIPv4DNS1, net_base::DNSClient::Error::kTimedOut);

  // Pump event loop to fire the async Retry().
  dispatcher_.task_environment().RunUntilIdle();

  // Retry attempt: DNS succeeds. Set up expectations before triggering
  // the DNS result that starts the HTTP request.
  const std::string resp = "Retry DNS success.";
  ExpectRequestSuccessCallback(resp);
  EXPECT_CALL(*transport(),
              ResolveHostToIp(url.host(), url.port(), "10.1.1.1"));
  ExpectCreateConnection(kTextURL, brillo::http::request_type::kGet);
  ExpectFinishRequestAsyncSuccess(resp);

  GetDNSResultSuccess(
      kIPv4DNS0, {net_base::IPAddress(net_base::IPv4Address(10, 1, 1, 1))});

  ExpectStopped();
}

TEST_F(HttpRequestTest, MaxRetriesExhausted) {
  CreateRequest(kInterfaceName, net_base::IPFamily::kIPv4,
                {kIPv4DNS0, kIPv4DNS1});

  HttpRequest::RetryPolicy policy;
  policy.max_retries = 1;
  ExpectRequestErrorCallback(brillo::http::TransportError::kIOError);
  EXPECT_CALL(*transport(),
              CreateConnection(Eq(kIPv4AddressURL),
                               brillo::http::request_type::kGet, _, "", "", _))
      .WillOnce(Return(brillo_connection_))
      .WillOnce(Return(brillo_connection_));
  EXPECT_CALL(*brillo_connection_, FinishRequestAsync(_, _))
      .Times(2)
      .WillRepeatedly(WithArg<1>([](auto callback) {
        brillo::ErrorPtr error;
        brillo::http::AddTransportError(
            &error, FROM_HERE, brillo::http::TransportError::kIOError, "");
        std::move(callback).Run(kNoRequestId, error.get());
        return 0;
      }));

  StartRequestWithRetry(kIPv4AddressURL, policy);

  // First failure posted retry async. Pump to fire retry.
  dispatcher_.task_environment().RunUntilIdle();

  // Second failure: max_retries exhausted, error callback fires.
  ExpectStopped();
}

TEST_F(HttpRequestTest, RetryWithDelay) {
  CreateRequest(kInterfaceName, net_base::IPFamily::kIPv4,
                {kIPv4DNS0, kIPv4DNS1});

  constexpr base::TimeDelta kDelay = base::Milliseconds(500);
  HttpRequest::RetryPolicy policy;
  policy.retry_delay = kDelay;

  const std::string resp = "Delayed retry success.";
  ExpectRequestSuccessCallback(resp);
  EXPECT_CALL(*transport(),
              CreateConnection(Eq(kIPv4AddressURL),
                               brillo::http::request_type::kGet, _, "", "", _))
      .WillOnce(Return(brillo_connection_))
      .WillOnce(Return(brillo_connection_));
  SetupFinishRequestFailThenSuccess(1, brillo::http::TransportError::kIOError,
                                    resp);

  StartRequestWithRetry(kIPv4AddressURL, policy);

  // Initial request has failed and a delayed retry is pending.
  // is_running() is true because the retry cycle is not yet complete.
  EXPECT_TRUE(request_->is_running());

  // Fast-forward past the delay to fire the delayed Retry().
  dispatcher_.task_environment().FastForwardBy(kDelay);

  ExpectStopped();
}

TEST_F(HttpRequestTest, CustomRetryPolicy) {
  CreateRequest(kInterfaceName, net_base::IPFamily::kIPv4,
                {kIPv4DNS0, kIPv4DNS1});

  HttpRequest::RetryPolicy policy;
  policy.max_retries = 2;
  policy.retryable_errors = {brillo::http::TransportError::kConnectionFailure};

  const std::string resp = "Custom policy success.";
  ExpectRequestSuccessCallback(resp);
  EXPECT_CALL(*transport(),
              CreateConnection(Eq(kIPv4AddressURL),
                               brillo::http::request_type::kGet, _, "", "", _))
      .WillOnce(Return(brillo_connection_))
      .WillOnce(Return(brillo_connection_))
      .WillOnce(Return(brillo_connection_));
  SetupFinishRequestFailThenSuccess(
      2, brillo::http::TransportError::kConnectionFailure, resp);

  StartRequestWithRetry(kIPv4AddressURL, policy);

  // Two retries needed. Pump after each async retry post.
  dispatcher_.task_environment().RunUntilIdle();

  ExpectStopped();
}

TEST_F(HttpRequestTest, StopDuringDelayedRetry) {
  CreateRequest(kInterfaceName, net_base::IPFamily::kIPv4,
                {kIPv4DNS0, kIPv4DNS1});

  constexpr base::TimeDelta kDelay = base::Seconds(1);
  HttpRequest::RetryPolicy policy;
  policy.retry_delay = kDelay;

  // Only one CreateConnection (the initial attempt). The retry must
  // NOT fire after `Stop()`.
  ExpectCreateConnection(kIPv4AddressURL, brillo::http::request_type::kGet);
  ExpectFinishRequestAsyncTransportError(
      brillo::http::TransportError::kIOError);

  StartRequestWithRetry(kIPv4AddressURL, policy);

  // Delayed retry is pending. Stop() cancels it via generation counter.
  request_->Stop();
  EXPECT_FALSE(request_->is_running());

  // Fast-forward past the delay. The stale Retry() callback fires but
  // checks retry_generation_ and returns immediately.
  dispatcher_.task_environment().FastForwardBy(kDelay);
  EXPECT_FALSE(request_->is_running());
}

TEST_F(HttpRequestTest, ReuseAfterRetryCompletes) {
  CreateRequest(kInterfaceName, net_base::IPFamily::kIPv4,
                {kIPv4DNS0, kIPv4DNS1});

  // First request: retry once on IO error, then succeed.
  {
    const std::string resp = "First request.";
    HttpRequest::RetryPolicy policy;
    ExpectRequestSuccessCallback(resp);
    EXPECT_CALL(*transport(), CreateConnection(Eq(kIPv4AddressURL),
                                               brillo::http::request_type::kGet,
                                               _, "", "", _))
        .WillOnce(Return(brillo_connection_))
        .WillOnce(Return(brillo_connection_));
    SetupFinishRequestFailThenSuccess(1, brillo::http::TransportError::kIOError,
                                      resp);

    StartRequestWithRetry(kIPv4AddressURL, policy);
    dispatcher_.task_environment().RunUntilIdle();
    ExpectStopped();
    Mock::VerifyAndClearExpectations(transport().get());
    Mock::VerifyAndClearExpectations(brillo_connection_.get());
  }

  // Second request: no retry policy. IO error should be reported
  // immediately (no stale retry from the first call).
  {
    ExpectRequestErrorCallback(
        brillo::http::TransportError::kConnectionFailure);
    ExpectCreateConnection(kIPv4AddressURL, brillo::http::request_type::kGet);
    ExpectFinishRequestAsyncFail();

    StartRequest(HttpRequest::Method::kGet, kIPv4AddressURL);
    ExpectStopped();
  }
}

TEST_F(HttpRequestTest, StopThenStartDuringDelayedRetry) {
  CreateRequest(kInterfaceName, net_base::IPFamily::kIPv4,
                {kIPv4DNS0, kIPv4DNS1});

  constexpr base::TimeDelta kDelay = base::Seconds(1);
  HttpRequest::RetryPolicy policy;
  policy.retry_delay = kDelay;

  // First request: fails, schedules delayed retry.
  ExpectCreateConnection(kIPv4AddressURL, brillo::http::request_type::kGet);
  ExpectFinishRequestAsyncTransportError(
      brillo::http::TransportError::kIOError);
  StartRequestWithRetry(kIPv4AddressURL, policy);
  Mock::VerifyAndClearExpectations(transport().get());
  Mock::VerifyAndClearExpectations(brillo_connection_.get());

  // Stop the first request, then start a new one (no retry).
  request_->Stop();

  const std::string resp = "Second request.";
  ExpectRequestSuccessCallback(resp);
  ExpectCreateConnection(kIPv4AddressURL, brillo::http::request_type::kGet);
  ExpectFinishRequestAsyncSuccess(resp);
  StartRequest(HttpRequest::Method::kGet, kIPv4AddressURL);

  // Fast-forward past the delay. The stale Retry() fires but the
  // generation check causes it to return immediately. The second
  // request must not be affected.
  dispatcher_.task_environment().FastForwardBy(kDelay);
  ExpectStopped();
}

}  // namespace shill
