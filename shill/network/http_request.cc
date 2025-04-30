// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/network/http_request.h"

#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <base/check.h>
#include <base/functional/bind.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <base/time/time.h>
#include <base/types/expected.h>
#include <brillo/http/http_utils.h>
#include <chromeos/net-base/dns_client.h>
#include <chromeos/net-base/http_url.h>
#include <curl/curl.h>

#include "shill/event_dispatcher.h"
#include "shill/logging.h"

namespace {

// The curl error domain for http requests
const char kCurlEasyError[] = "curl_easy_error";

// Maximum number of name servers queried in parallel.
constexpr int kDNSMaxParallelQueries = 4;
// Maximum number of query tries per name server.
constexpr int kDNSNumberOfQueries = 3;
// Timeout of a single query to a single name server.
constexpr base::TimeDelta kDNSTimeoutOfQueries = base::Seconds(2);

}  // namespace

namespace shill {

namespace Logging {
static auto kModuleLogScope = ScopeLogger::kHTTP;
static std::string ObjectID(const HttpRequest* r) {
  return r->logging_tag();
}
}  // namespace Logging

HttpRequest::HttpRequest(
    EventDispatcher* dispatcher,
    std::string_view interface_name,
    net_base::IPFamily ip_family,
    const std::vector<net_base::IPAddress>& dns_list,
    bool allow_non_google_https,
    std::shared_ptr<brillo::http::Transport> transport,
    std::unique_ptr<net_base::DNSClientFactory> dns_client_factory)
    : dispatcher_(dispatcher),
      ip_family_(ip_family),
      dns_list_(dns_list),
      transport_(transport),
      dns_client_factory_(std::move(dns_client_factory)),
      request_id_(-1),
      is_running_(false) {
  dns_options_.interface = interface_name;
  // TODO(b/307880493): Tune these parameters based on the technology once
  // metrics are available.
  dns_options_.number_of_tries = kDNSNumberOfQueries;
  dns_options_.per_query_initial_timeout = kDNSTimeoutOfQueries;
  // b/180521518, Force the transport to bind to |interface_name|. Otherwise,
  // the request would be routed by default through the current physical default
  // network. b/288351302: binding to an IP address of the interface name is not
  // enough to disambiguate all IPv4 multi-network scenarios.
  transport_->SetInterface(dns_options_.interface);
  if (allow_non_google_https) {
    transport_->UseCustomCertificate(
        brillo::http::Transport::Certificate::kNss);
  }
}

HttpRequest::~HttpRequest() {
  Stop();
}

void HttpRequest::Start(std::string_view logging_tag,
                        const net_base::HttpUrl& url,
                        const brillo::http::HeaderList& headers,
                        base::OnceCallback<void(Result result)> callback) {
  DCHECK(!is_running_);
  logging_tag_ = logging_tag;
  url_ = url;
  headers_ = headers;
  is_running_ = true;
  transport_->SetDefaultTimeout(kRequestTimeout);
  callback_ = std::move(callback);

  // Name resolution is not needed if the hostname is an IP address literal.
  if (const auto server_addr =
          net_base::IPAddress::CreateFromString(url_.host())) {
    if (server_addr->GetFamily() == ip_family_) {
      StartRequest();
    } else {
      LOG(ERROR) << logging_tag_ << " " << __func__ << ": Server hostname "
                 << url_.host() << " doesn't match the IP family "
                 << ip_family_;
      SendErrorAsync(Error::kDNSFailure);
    }
    return;
  }

  for (const auto& dns : dns_list_) {
    if (dns_queries_.size() > kDNSMaxParallelQueries) {
      break;
    }
    auto cb = base::BindOnce(&HttpRequest::GetDNSResult,
                             weak_ptr_factory_.GetWeakPtr(), dns);
    auto dns_options = dns_options_;
    dns_options.name_server = dns;
    dns_queries_[dns] = dns_client_factory_->Resolve(
        ip_family_, url_.host(), std::move(cb), dns_options);
  }
}

void HttpRequest::StartRequest() {
  std::string url_string = url_.ToString();
  SLOG(this, 2) << logging_tag_ << " " << __func__ << ": Starting request to "
                << url_string;
  request_id_ = brillo::http::Get(
      url_string, headers_, transport_,
      base::BindOnce(&HttpRequest::OnSuccess, weak_ptr_factory_.GetWeakPtr()),
      base::BindOnce(&HttpRequest::OnError, weak_ptr_factory_.GetWeakPtr()));
}

void HttpRequest::OnSuccess(brillo::http::RequestID request_id,
                            std::unique_ptr<brillo::http::Response> response) {
  if (request_id != request_id_) {
    LOG(ERROR) << logging_tag_ << " " << __func__ << ": Expected request ID "
               << request_id_ << " but got " << request_id;
    SendError(Error::kInternalError);
    return;
  }

  auto callback = std::move(callback_);
  Stop();

  if (!callback.is_null()) {
    std::move(callback).Run(std::move(response));
  }
}

void HttpRequest::OnError(brillo::http::RequestID request_id,
                          const brillo::Error* error) {
  int error_code;
  if (error->GetDomain() != kCurlEasyError) {
    LOG(ERROR) << logging_tag_ << " " << __func__ << ": Expected error domain "
               << kCurlEasyError << " but got " << error->GetDomain();
    SendError(Error::kInternalError);
    return;
  }
  if (request_id != request_id_) {
    LOG(ERROR) << logging_tag_ << " " << __func__ << ": Expected request ID "
               << request_id_ << " but got " << request_id;
    SendError(Error::kInternalError);
    return;
  }
  if (!base::StringToInt(error->GetCode(), &error_code)) {
    LOG(ERROR) << logging_tag_ << " " << __func__
               << ": Unable to convert error code " << error->GetCode()
               << " to Int";
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
      LOG(ERROR) << logging_tag_ << " " << __func__
                 << ": Unknown curl error code " << error_code;
      SendError(Error::kInternalError);
  }
}

void HttpRequest::Stop() {
  if (!is_running_) {
    return;
  }
  dns_queries_.clear();
  is_running_ = false;
  request_id_ = -1;
  callback_.Reset();
}

// DnsClient callback that fires when the DNS request completes.
void HttpRequest::GetDNSResult(net_base::IPAddress dns,
                               base::TimeDelta duration,
                               const net_base::DNSClient::Result& result) {
  if (!result.has_value()) {
    LOG(WARNING) << logging_tag_ << " " << __func__ << ": Could not resolve "
                 << url_.host() << " with " << dns << ": " << result.error();
    auto error = Error::kDNSFailure;
    if (result.error() == net_base::DNSClient::Error::kTimedOut) {
      error = Error::kDNSTimeout;
    }
    dns_queries_.erase(dns);
    if (dns_queries_.empty()) {
      SendError(error);
    }
    return;
  }

  // Cancel all other queries.
  dns_queries_.clear();

  // CURLOPT_RESOLVE expects the format "[+]HOST:PORT:ADDRESS[,ADDRESS]" for DNS
  // cache entries, and brillo::http::Transport::ResolveHostToIp() already adds
  // "HOST:PORT:".
  std::string addresses;
  std::string sep = "";
  for (const auto& addr : *result) {
    base::StrAppend(&addresses, {sep, addr.ToString()});
    sep = ",";
  }
  // Add the host/port to IP mapping to the DNS cache to force curl to resolve
  // the URL to the given IP. Otherwise, curl will do its own DNS resolution.
  transport_->ResolveHostToIp(url_.host(), url_.port(), addresses);
  LOG(INFO) << logging_tag_ << " " << __func__ << ": Resolved " << url_.host()
            << " to " << addresses << " in " << duration;
  StartRequest();
}

void HttpRequest::SendError(Error error) {
  // Save copies on the stack, since Stop() will remove them.
  auto callback = std::move(callback_);
  Stop();
  // Call the callback last, since it may delete us and |this| may no longer
  // be valid.
  if (!callback.is_null()) {
    std::move(callback).Run(base::unexpected(error));
  }
}

void HttpRequest::SendErrorAsync(Error error) {
  dispatcher_->PostTask(FROM_HERE,
                        base::BindOnce(&HttpRequest::SendError,
                                       weak_ptr_factory_.GetWeakPtr(), error));
}

// static
std::string_view ErrorName(HttpRequest::Error error) {
  switch (error) {
    case HttpRequest::Error::kInternalError:
      return "Internal error";
    case HttpRequest::Error::kDNSFailure:
      return "DNS failure";
    case HttpRequest::Error::kDNSTimeout:
      return "DNS timeout";
    case HttpRequest::Error::kConnectionFailure:
      return "Connection failure";
    case HttpRequest::Error::kTLSFailure:
      return "TLS failure";
    case HttpRequest::Error::kIOError:
      return "IO error";
    case HttpRequest::Error::kHTTPTimeout:
      return "Request timeout";
  }
}

std::ostream& operator<<(std::ostream& stream, HttpRequest::Error error) {
  return stream << ErrorName(error);
}

std::ostream& operator<<(std::ostream& stream,
                         std::optional<HttpRequest::Error> error) {
  return stream << (error ? ErrorName(*error) : "Success");
}

}  // namespace shill
