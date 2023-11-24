// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/http_request.h"

#include <curl/curl.h>

#include <optional>
#include <string>
#include <utility>

#include <base/check.h>
#include <base/functional/bind.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/time/time.h>
#include <brillo/http/http_utils.h>
#include <net-base/http_url.h>

#include "shill/dns_client.h"
#include "shill/error.h"
#include "shill/event_dispatcher.h"
#include "shill/logging.h"

namespace {

// The curl error domain for http requests
const char kCurlEasyError[] = "curl_easy_error";

}  // namespace

namespace shill {

namespace Logging {
static auto kModuleLogScope = ScopeLogger::kHTTP;
static std::string ObjectID(const HttpRequest* r) {
  return r->logging_tag();
}
}  // namespace Logging

HttpRequest::HttpRequest(EventDispatcher* dispatcher,
                         const std::string& interface_name,
                         net_base::IPFamily ip_family,
                         const std::vector<net_base::IPAddress>& dns_list,
                         bool allow_non_google_https)
    : ip_family_(ip_family),
      dns_list_(dns_list),
      weak_ptr_factory_(this),
      dns_client_callback_(base::BindRepeating(&HttpRequest::GetDNSResult,
                                               weak_ptr_factory_.GetWeakPtr())),
      dns_client_(new DnsClient(ip_family_,
                                interface_name,
                                DnsClient::kDnsTimeout,
                                dispatcher,
                                dns_client_callback_)),
      transport_(brillo::http::Transport::CreateDefault()),
      request_id_(-1),
      is_running_(false) {
  // b/180521518, Force the transport to bind to |interface_name|. Otherwise,
  // the request would be routed by default through the current physical default
  // network. b/288351302: binding to an IP address of the interface name is not
  // enough to disambiguate all IPv4 multi-network scenarios.
  transport_->SetInterface(interface_name);
  if (allow_non_google_https) {
    transport_->UseCustomCertificate(
        brillo::http::Transport::Certificate::kNss);
  }
}

HttpRequest::~HttpRequest() {
  Stop();
}

std::optional<HttpRequest::Error> HttpRequest::Start(
    const std::string& logging_tag,
    const net_base::HttpUrl& url,
    const brillo::http::HeaderList& headers,
    base::OnceCallback<void(std::shared_ptr<brillo::http::Response>)>
        request_success_callback,
    base::OnceCallback<void(Error)> request_error_callback) {
  SLOG(this, 3) << "In " << __func__;

  DCHECK(!is_running_);

  logging_tag_ = logging_tag;
  url_ = url;
  headers_ = headers;
  is_running_ = true;
  transport_->SetDefaultTimeout(kRequestTimeout);

  request_success_callback_ = std::move(request_success_callback);
  request_error_callback_ = std::move(request_error_callback);

  const auto server_addr = net_base::IPAddress::CreateFromString(url_.host());
  if (server_addr && server_addr->GetFamily() != ip_family_) {
    LOG(ERROR) << logging_tag_ << ": Server hostname " << url_.host()
               << " doesn't match the IP family " << ip_family_;
    Stop();
    return Error::kDNSFailure;
  }

  if (server_addr) {
    StartRequest();
  } else {
    SLOG(this, 2) << "Looking up host: " << url_.host();
    shill::Error error;
    std::vector<std::string> dns_addresses;
    // TODO(b/307880493): Migrate to net_base::DNSClient and avoid
    // converting the net_base::IPAddress values to std::string.
    for (const auto& dns : dns_list_) {
      if (dns.GetFamily() == ip_family_) {
        dns_addresses.push_back(dns.ToString());
      }
    }
    if (!dns_client_->Start(dns_addresses, url_.host(), &error)) {
      LOG(ERROR) << logging_tag_
                 << ": Failed to start DNS client: " << error.message();
      Stop();
      return Error::kDNSFailure;
    }
  }

  return std::nullopt;
}

void HttpRequest::StartRequest() {
  std::string url_string = url_.ToString();
  SLOG(this, 2) << logging_tag_ << ": Starting request to " << url_string;
  request_id_ =
      brillo::http::Get(url_string, headers_, transport_,
                        base::BindOnce(&HttpRequest::SuccessCallback,
                                       weak_ptr_factory_.GetWeakPtr()),
                        base::BindOnce(&HttpRequest::ErrorCallback,
                                       weak_ptr_factory_.GetWeakPtr()));
}

void HttpRequest::SuccessCallback(
    brillo::http::RequestID request_id,
    std::unique_ptr<brillo::http::Response> response) {
  if (request_id != request_id_) {
    LOG(ERROR) << logging_tag_ << ": Expected request ID " << request_id_
               << " but got " << request_id;
    SendError(Error::kInternalError);
    return;
  }

  base::OnceCallback<void(std::shared_ptr<brillo::http::Response>)>
      request_success_callback = std::move(request_success_callback_);
  Stop();

  if (!request_success_callback.is_null()) {
    std::move(request_success_callback).Run(std::move(response));
  }
}

void HttpRequest::ErrorCallback(brillo::http::RequestID request_id,
                                const brillo::Error* error) {
  int error_code;
  if (error->GetDomain() != kCurlEasyError) {
    LOG(ERROR) << logging_tag_ << ": Expected error domain " << kCurlEasyError
               << " but got " << error->GetDomain();
    SendError(Error::kInternalError);
    return;
  }
  if (request_id != request_id_) {
    LOG(ERROR) << logging_tag_ << ": Expected request ID " << request_id_
               << " but got " << request_id;
    SendError(Error::kInternalError);
    return;
  }
  if (!base::StringToInt(error->GetCode(), &error_code)) {
    LOG(ERROR) << logging_tag_ << ": Unable to convert error code "
               << error->GetCode() << " to Int";
    SendError(Error::kInternalError);
    return;
  }

  // TODO(matthewmwang): This breaks abstraction. Modify brillo::http::Transport
  // to provide an implementation agnostic error code.
  switch (error_code) {
    case CURLE_COULDNT_CONNECT:
      SendError(Error::kConnectionFailure);
      break;
    case CURLE_PEER_FAILED_VERIFICATION:
      SendError(Error::kTLSFailure);
      break;
    case CURLE_WRITE_ERROR:
    case CURLE_READ_ERROR:
      SendError(Error::kIOError);
      break;
    case CURLE_OPERATION_TIMEDOUT:
      SendError(Error::kHTTPTimeout);
      break;
    default:
      LOG(ERROR) << logging_tag_ << ": Unknown curl error code " << error_code;
      SendError(Error::kInternalError);
  }
}

void HttpRequest::Stop() {
  SLOG(this, 3) << "In " << __func__ << ": running is " << is_running_;

  if (!is_running_) {
    return;
  }

  // Clear IO handlers first so that closing the socket doesn't cause
  // events to fire.
  dns_client_->Stop();
  is_running_ = false;
  request_id_ = -1;
  request_error_callback_.Reset();
  request_success_callback_.Reset();
}

// DnsClient callback that fires when the DNS request completes.
void HttpRequest::GetDNSResult(
    const base::expected<net_base::IPAddress, shill::Error>& address) {
  SLOG(this, 3) << "In " << __func__;
  if (!address.has_value()) {
    LOG(ERROR) << logging_tag_ << ": Could not resolve " << url_.host() << ": "
               << address.error().message();
    if (address.error().message() == DnsClient::kErrorTimedOut) {
      SendError(Error::kDNSTimeout);
    } else {
      SendError(Error::kDNSFailure);
    }
    return;
  }

  // Add the host/port to IP mapping to the DNS cache to force curl to resolve
  // the URL to the given IP. Otherwise, will do its own DNS resolution and not
  // use the IP we provide to it.
  transport_->ResolveHostToIp(url_.host(), url_.port(), address->ToString());
  LOG(INFO) << logging_tag_ << ": Resolved " << url_.host() << " to "
            << *address;
  StartRequest();
}

void HttpRequest::SendError(Error error) {
  // Save copies on the stack, since Stop() will remove them.
  auto request_error_callback = std::move(request_error_callback_);
  Stop();

  // Call the callback last, since it may delete us and |this| may no longer
  // be valid.
  if (!request_error_callback.is_null()) {
    std::move(request_error_callback).Run(error);
  }
}

std::ostream& operator<<(std::ostream& stream, HttpRequest::Error error) {
  switch (error) {
    case HttpRequest::Error::kInternalError:
      return stream << "Internal error";
    case HttpRequest::Error::kDNSFailure:
      return stream << "DNS failure";
    case HttpRequest::Error::kDNSTimeout:
      return stream << "DNS timeout";
    case HttpRequest::Error::kConnectionFailure:
      return stream << "Connection failure";
    case HttpRequest::Error::kTLSFailure:
      return stream << "TLS failure";
    case HttpRequest::Error::kIOError:
      return stream << "IO error";
    case HttpRequest::Error::kHTTPTimeout:
      return stream << "Request timeout";
  }
}

}  // namespace shill
