// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_HTTP_REQUEST_H_
#define SHILL_HTTP_REQUEST_H_

#include <map>
#include <memory>
#include <optional>
#include <ostream>
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
#include <net-base/dns_client.h>
#include <net-base/http_url.h>
#include <net-base/ip_address.h>

namespace shill {

class Error;
class EventDispatcher;

// The HttpRequest class implements facilities for performing a simple "GET"
// request and returning the contents via a callback. By default, this class
// will only be allowed to communicate with Google servers when secure (HTTPS)
// communication is used.
class HttpRequest {
 public:
  // Represents possible errors for an HTTP requests.
  enum class Error {
    // Internal error: unknown error, incorrect request id, error code
    // conversion error, ...
    kInternalError,
    // Name resolution failed.
    kDNSFailure,
    // Name resolution timed out.
    kDNSTimeout,
    // The HTTP connection failed.
    kConnectionFailure,
    // The TLS connection failed.
    kTLSFailure,
    // The HTTP network IO failed.
    kIOError,
    // The HTTP request timed out.
    kHTTPTimeout,
  };

  static std::string_view ErrorName(Error error);

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

  // Start an http GET request to the URL |url|. If the request succeeds,
  // |request_success_callback| is called asynchronously with the response data.
  // Otherwise, if the request fails |request_error_callback| is called with the
  // error reason.
  virtual void Start(
      std::string_view logging_tag,
      const net_base::HttpUrl& url,
      const brillo::http::HeaderList& headers,
      base::OnceCallback<void(std::shared_ptr<brillo::http::Response>)>
          request_success_callback,
      base::OnceCallback<void(Error)> request_error_callback);

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
  void SuccessCallback(brillo::http::RequestID request_id,
                       std::unique_ptr<brillo::http::Response> response);
  void ErrorCallback(brillo::http::RequestID request_id,
                     const brillo::Error* error);
  // Calls synchronously |request_error_callback_| with |error| and terminates
  // this request.
  void SendError(Error error);
  // Same as SendError, but asynchrously using |dispatcher_|.
  void SendErrorAsync(Error error);

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
  // All DNS queries currently in-flight, keyed by the DNS server address.
  std::map<net_base::IPAddress, std::unique_ptr<net_base::DNSClient>>
      dns_queries_;
  std::string logging_tag_;
  net_base::HttpUrl url_;
  brillo::http::HeaderList headers_;
  base::OnceCallback<void(Error)> request_error_callback_;
  base::OnceCallback<void(std::shared_ptr<brillo::http::Response>)>
      request_success_callback_;

  base::WeakPtrFactory<HttpRequest> weak_ptr_factory_{this};
};

std::ostream& operator<<(std::ostream& stream, HttpRequest::Error error);
std::ostream& operator<<(std::ostream& stream,
                         std::optional<HttpRequest::Error> error);

}  // namespace shill

#endif  // SHILL_HTTP_REQUEST_H_
