// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_NETWORK_HTTP_REQUEST_H_
#define SHILL_NETWORK_HTTP_REQUEST_H_

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include <base/cancelable_callback.h>
#include <base/functional/callback.h>
#include <base/memory/ref_counted.h>
#include <base/memory/weak_ptr.h>
#include <base/types/expected.h>
#include <brillo/errors/error.h>
#include <brillo/http/http_transport.h>
#include <brillo/http/http_transport_error.h>
#include <chromeos/net-base/dns_client.h>
#include <chromeos/net-base/http_url.h>
#include <chromeos/net-base/ip_address.h>

namespace shill {

class Error;
class EventDispatcher;

// The HttpRequest class implements facilities for performing a simple HTTP
// request (GET or HEAD) and returning the contents via a callback. By default,
// this class will only be allowed to communicate with Google servers when
// secure (HTTPS) communication is used.
class HttpRequest {
 public:
  // The client callback returns the HTTP response on success, or a
  // TransportError.
  using Result = base::expected<std::unique_ptr<brillo::http::Response>,
                                brillo::http::TransportError>;

  // The HTTP method to use for the request.
  enum class Method {
    kGet,
    kHead,
  };

  // Configures automatic retry behavior. When provided to `Start()`,
  // transient errors matching `retryable_errors` are retried up to
  // `max_retries` times with `retry_delay` between attempts. The
  // initial attempt is not counted as a retry. Setting `max_retries`
  // to 0 disables retry even when a policy is provided.
  struct RetryPolicy {
    // Default number of automatic retries after the initial attempt.
    static constexpr size_t kDefaultAutoRetry = 1;

    // Maximum number of retries after the initial attempt.
    // E.g., max_retries=1 means up to 2 total attempts.
    size_t max_retries = kDefaultAutoRetry;

    // Delay between retry attempts. Zero means retry immediately.
    base::TimeDelta retry_delay = base::TimeDelta();

    // Errors that trigger a retry. Only errors in this set are retried;
    // all others are reported immediately to the caller.
    std::set<brillo::http::TransportError> retryable_errors = {
        brillo::http::TransportError::kIOError,
        brillo::http::TransportError::kDnsTimeout,
    };
  };

  // |allow_non_google_https| determines whether or not secure (HTTPS)
  // communication with a non-Google server is allowed. Note that this
  // will not change any behavior for HTTP communication.
  HttpRequest(EventDispatcher* dispatcher,
              std::string_view interface_name,
              net_base::IPFamily ip_family,
              const std::vector<net_base::IPAddress>& dns_list,
              bool allow_non_google_https = false,
              std::shared_ptr<brillo::http::Transport> transport =
                  brillo::http::Transport::CreateDefault(),
              std::unique_ptr<net_base::DNSClientFactory> dns_client_factory =
                  std::make_unique<net_base::DNSClientFactory>());
  HttpRequest(const HttpRequest&) = delete;
  HttpRequest& operator=(const HttpRequest&) = delete;

  virtual ~HttpRequest();

  // Starts an HTTP request with the given `method` to `url`. Calls `callback`
  // asynchronously with the response data or error. If `timeout` is not
  // provided, zero, or negative, uses the default 10s timeout. If
  // `retry_policy` is provided, retries retryable errors up to
  // `retry_policy.max_retries` times. For hostname URLs, each retry performs
  // full DNS re-resolution. For numeric IP URLs, each retry re-issues the
  // HTTP request directly.
  virtual void Start(Method method,
                     std::string_view logging_tag,
                     const net_base::HttpUrl& url,
                     const brillo::http::HeaderList& headers,
                     base::OnceCallback<void(Result result)> callback,
                     std::optional<base::TimeDelta> timeout = std::nullopt,
                     std::optional<RetryPolicy> retry_policy = std::nullopt);

  // Stop the current HttpRequest.  No callback is called as a side
  // effect of this function.
  virtual void Stop();

  virtual const std::string& logging_tag() const { return logging_tag_; }
  bool is_running() const { return is_running_; }

 private:
  friend class HttpRequestTest;

  // Time to wait for HTTP request.
  static constexpr base::TimeDelta kRequestTimeout = base::Seconds(10);

  void GetDNSResult(net_base::IPAddress dns,
                    base::TimeDelta duration,
                    const net_base::DNSClient::Result& result);
  void StartRequest();
  void OnSuccess(brillo::http::RequestID request_id,
                 std::unique_ptr<brillo::http::Response> response);
  void OnError(brillo::http::RequestID request_id, const brillo::Error* error);
  // Calls synchronously |request_error_callback_| with |error| and terminates
  // this request.
  void SendError(brillo::http::TransportError error);
  // Same as SendError, but asynchrously using |dispatcher_|.
  void SendErrorAsync(brillo::http::TransportError error);

  // Resets connection-level state (DNS queries, request ID, running flag)
  // without clearing request parameters, callback, or retry state. Used
  // by `Retry()` to clean up the current attempt before re-starting.
  void ResetConnection();

  // Performs the actual start logic shared by `Start()` (public) and
  // `Retry()` (internal). Sets `is_running_`, stores request params,
  // and initiates DNS resolution or direct HTTP request.
  void StartInternal(std::string_view logging_tag,
                     const net_base::HttpUrl& url,
                     const brillo::http::HeaderList& headers,
                     base::OnceCallback<void(Result result)> callback);

  // Calls `ResetConnection()` then `StartInternal()` with the preserved
  // request parameters and callback. Checks `retry_generation_` to abort
  // if the request was externally stopped or restarted since this retry
  // was scheduled.
  void Retry(size_t generation);

  // If `error` is retryable and retry budget remains, schedules a retry
  // and returns. Otherwise calls `SendError(error)` to report the error
  // to the caller and stop the request.
  void MaybeRetryOrSendError(brillo::http::TransportError error);

  // Checks whether `error` should trigger a retry based on
  // `retry_policy_` and `retry_count_`. Returns true if a retry was
  // initiated (caller should return without calling `SendError`).
  bool MaybeRetry(brillo::http::TransportError error);

  EventDispatcher* dispatcher_;
  net_base::IPFamily ip_family_;
  // The list of name server addresses.
  std::vector<net_base::IPAddress> dns_list_;
  std::shared_ptr<brillo::http::Transport> transport_;
  std::unique_ptr<net_base::DNSClientFactory> dns_client_factory_;
  // Common DNS settings for all DNS queries.
  net_base::DNSClient::Options dns_options_;
  brillo::http::RequestID request_id_;
  bool is_running_;
  Method method_;
  // All DNS queries currently in-flight, keyed by the DNS server address.
  std::map<net_base::IPAddress, std::unique_ptr<net_base::DNSClient>>
      dns_queries_;
  std::string logging_tag_;
  net_base::HttpUrl url_;
  brillo::http::HeaderList headers_;
  base::OnceCallback<void(Result result)> callback_;

  // Custom timeout for the current request. std::nullopt means use
  // the default `kRequestTimeout`.
  std::optional<base::TimeDelta> timeout_;
  // Retry configuration for the current request. std::nullopt means
  // no retry (single attempt only).
  std::optional<RetryPolicy> retry_policy_;
  // Number of retries completed so far. Reset to 0 in `Start()`.
  size_t retry_count_ = 0;
  // Monotonically increasing counter incremented in `Stop()` and
  // `Start()`. Used to invalidate stale delayed retry callbacks when
  // the request is externally stopped or restarted before a delayed
  // `Retry()` fires.
  size_t retry_generation_ = 0;

  base::WeakPtrFactory<HttpRequest> weak_ptr_factory_{this};
};

// Outputs the string representation of `method` (e.g. "GET", "HEAD").
std::ostream& operator<<(std::ostream& stream, HttpRequest::Method method);

}  // namespace shill

#endif  // SHILL_NETWORK_HTTP_REQUEST_H_
