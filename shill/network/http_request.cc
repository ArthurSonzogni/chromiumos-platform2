// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/network/http_request.h"

#include <string>
#include <string_view>
#include <utility>

#include <base/check.h>
#include <base/functional/bind.h>
#include <base/logging.h>
#include <base/strings/string_util.h>
#include <base/time/time.h>
#include <base/types/expected.h>
#include <brillo/http/http_transport_error.h>
#include <brillo/http/http_utils.h>
#include <chromeos/net-base/dns_client.h>
#include <chromeos/net-base/http_url.h>

#include "shill/event_dispatcher.h"
#include "shill/logging.h"

namespace {

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
      is_running_(false),
      method_(Method::kGet) {
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

void HttpRequest::Start(Method method,
                        std::string_view logging_tag,
                        const net_base::HttpUrl& url,
                        const brillo::http::HeaderList& headers,
                        base::OnceCallback<void(Result result)> callback,
                        std::optional<base::TimeDelta> timeout,
                        std::optional<RetryPolicy> retry_policy) {
  // Always reset retry state on external `Start()` calls. This prevents
  // stale `retry_policy_` or `retry_count_` from a previous request cycle
  // leaking into a new one (e.g., `Start()` -> error -> `Stop()` ->
  // `Start()` without retry).
  retry_policy_ = std::move(retry_policy);
  retry_count_ = 0;
  // Invalidate any stale delayed `Retry()` callbacks from a previous
  // request cycle, as defense-in-depth for release builds where
  // `DCHECK(!is_running_)` in `StartInternal()` is compiled out.
  retry_generation_++;
  timeout_ = timeout;
  method_ = method;

  StartInternal(logging_tag, url, headers, std::move(callback));
}

void HttpRequest::StartInternal(
    std::string_view logging_tag,
    const net_base::HttpUrl& url,
    const brillo::http::HeaderList& headers,
    base::OnceCallback<void(Result result)> callback) {
  DCHECK(!is_running_);
  logging_tag_ = logging_tag;
  url_ = url;
  headers_ = headers;
  is_running_ = true;
  request_id_ = -1;
  const base::TimeDelta effective_timeout =
      (timeout_.has_value() && timeout_.value() > base::TimeDelta())
          ? timeout_.value()
          : kRequestTimeout;
  transport_->SetDefaultTimeout(effective_timeout);
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
      // IP family mismatch is a configuration error that retrying cannot
      // fix. `MaybeRetry` is intentionally not called.
      SendErrorAsync(brillo::http::TransportError::kDnsFailure);
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
  switch (method_) {
    case Method::kGet:
      request_id_ =
          brillo::http::Get(url_string, headers_, transport_,
                            base::BindOnce(&HttpRequest::OnSuccess,
                                           weak_ptr_factory_.GetWeakPtr()),
                            base::BindOnce(&HttpRequest::OnError,
                                           weak_ptr_factory_.GetWeakPtr()));
      break;
    case Method::kHead:
      request_id_ =
          brillo::http::Head(url_string, transport_,
                             base::BindOnce(&HttpRequest::OnSuccess,
                                            weak_ptr_factory_.GetWeakPtr()),
                             base::BindOnce(&HttpRequest::OnError,
                                            weak_ptr_factory_.GetWeakPtr()));
      break;
  }
}

void HttpRequest::OnSuccess(brillo::http::RequestID request_id,
                            std::unique_ptr<brillo::http::Response> response) {
  if (request_id != request_id_) {
    LOG(ERROR) << logging_tag_ << " " << __func__ << ": Expected request ID "
               << request_id_ << " but got " << request_id;
    // Request ID mismatch is a programming error, not a transient network
    // failure. Retrying would not fix it, so `MaybeRetry` is not called.
    SendError(brillo::http::TransportError::kInternalError);
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
  if (request_id != request_id_) {
    LOG(ERROR) << logging_tag_ << " " << __func__ << ": Expected request ID "
               << request_id_ << " but got " << request_id;
    SendError(brillo::http::TransportError::kInternalError);
    return;
  }
  MaybeRetryOrSendError(brillo::http::ClassifyTransportError(error).value_or(
      brillo::http::TransportError::kUnknown));
}

void HttpRequest::ResetConnection() {
  dns_queries_.clear();
  is_running_ = false;
  request_id_ = -1;
}

void HttpRequest::Stop() {
  if (!is_running_) {
    return;
  }
  ResetConnection();
  callback_.Reset();
  timeout_.reset();
  retry_policy_.reset();
  retry_count_ = 0;
  // Invalidate any pending delayed `Retry()` callbacks. The generation
  // check in `Retry()` will cause stale callbacks to return immediately.
  retry_generation_++;
}

// DnsClient callback that fires when the DNS request completes.
void HttpRequest::GetDNSResult(net_base::IPAddress dns,
                               base::TimeDelta duration,
                               const net_base::DNSClient::Result& result) {
  if (!result.has_value()) {
    LOG(WARNING) << logging_tag_ << " " << __func__ << ": Could not resolve "
                 << url_.host() << " with " << dns << ": " << result.error();
    auto error = brillo::http::TransportError::kDnsFailure;
    if (result.error() == net_base::DNSClient::Error::kTimedOut) {
      error = brillo::http::TransportError::kDnsTimeout;
    }
    dns_queries_.erase(dns);
    if (dns_queries_.empty()) {
      MaybeRetryOrSendError(error);
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

void HttpRequest::SendError(brillo::http::TransportError error) {
  // Save copies on the stack, since Stop() will remove them.
  auto callback = std::move(callback_);
  Stop();
  // Call the callback last, since it may delete us and |this| may no longer
  // be valid.
  if (!callback.is_null()) {
    std::move(callback).Run(base::unexpected(error));
  }
}

void HttpRequest::SendErrorAsync(brillo::http::TransportError error) {
  dispatcher_->PostTask(FROM_HERE,
                        base::BindOnce(&HttpRequest::SendError,
                                       weak_ptr_factory_.GetWeakPtr(), error));
}

bool HttpRequest::MaybeRetry(brillo::http::TransportError error) {
  if (!retry_policy_.has_value()) {
    return false;
  }
  if (retry_count_ >= retry_policy_->max_retries) {
    return false;
  }
  if (!retry_policy_->retryable_errors.contains(error)) {
    return false;
  }
  retry_count_++;
  SLOG(this, 2) << logging_tag_ << " " << __func__ << ": retry " << retry_count_
                << "/" << retry_policy_->max_retries;
  if (retry_policy_->retry_delay.is_positive()) {
    // Post delayed retry with current generation. If `Stop()` is called
    // before the delay expires, `retry_generation_` increments and the
    // stale `Retry()` call is a no-op.
    dispatcher_->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&HttpRequest::Retry, weak_ptr_factory_.GetWeakPtr(),
                       retry_generation_),
        retry_policy_->retry_delay);
  } else {
    // Always post asynchronously to avoid re-entering the transport
    // from within an error callback.
    dispatcher_->PostTask(
        FROM_HERE,
        base::BindOnce(&HttpRequest::Retry, weak_ptr_factory_.GetWeakPtr(),
                       retry_generation_));
  }
  return true;
}

void HttpRequest::MaybeRetryOrSendError(brillo::http::TransportError error) {
  if (!MaybeRetry(error)) {
    SendError(error);
  }
}

void HttpRequest::Retry(size_t generation) {
  // Guard against stale delayed retry: if `Stop()` or a new `Start()` was
  // called since this retry was scheduled, `retry_generation_` will have
  // incremented and this callback is obsolete.
  if (generation != retry_generation_) {
    return;
  }

  ResetConnection();
  StartInternal(logging_tag_, url_, headers_, std::move(callback_));
}

std::ostream& operator<<(std::ostream& stream, HttpRequest::Method method) {
  switch (method) {
    case HttpRequest::Method::kGet:
      return stream << "GET";
    case HttpRequest::Method::kHead:
      return stream << "HEAD";
  }
}

}  // namespace shill
