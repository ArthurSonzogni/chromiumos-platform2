// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_HTTP_REQUEST_H_
#define SHILL_HTTP_REQUEST_H_

#include <memory>
#include <string>
#include <vector>

#include <base/cancelable_callback.h>
#include <base/functional/callback.h>
#include <base/memory/ref_counted.h>
#include <base/memory/weak_ptr.h>
#include <base/types/expected.h>
#include <brillo/errors/error.h>
#include <brillo/http/http_transport.h>
#include <net-base/ip_address.h>

#include "shill/dns_client.h"
#include "shill/http_url.h"

namespace shill {

class Error;
class EventDispatcher;

// The HttpRequest class implements facilities for performing a simple "GET"
// request and returning the contents via a callback. By default, this class
// will only be allowed to communicate with Google servers when secure (HTTPS)
// communication is used.
class HttpRequest {
 public:
  enum Result {
    kResultUnknown,
    kResultInvalidInput,
    kResultInProgress,
    kResultDNSFailure,
    kResultDNSTimeout,
    kResultConnectionFailure,
    kResultHTTPFailure,
    kResultHTTPTimeout,
    kResultSuccess
  };

  // |allow_non_google_https| determines whether or not secure (HTTPS)
  // communication with a non-Google server is allowed. Note that this
  // will not change any behavior for HTTP communication.
  HttpRequest(EventDispatcher* dispatcher,
              const std::string& interface_name,
              net_base::IPFamily ip_family,
              const std::vector<net_base::IPAddress>& dns_list,
              bool allow_non_google_https = false);
  HttpRequest(const HttpRequest&) = delete;
  HttpRequest& operator=(const HttpRequest&) = delete;

  virtual ~HttpRequest();

  // Start an http GET request to the URL |url|. If the request succeeds,
  // |request_success_callback| is called with the response data.
  // Otherwise, request_error_callback is called with the error reason.
  //
  // This (Start) function returns kResultDNSFailure  if the request fails to
  // initialize the DNS client, or kResultInProgress if the request
  // has started successfully and is now in progress.
  virtual Result Start(
      const std::string& logging_tag,
      const HttpUrl& url,
      const brillo::http::HeaderList& headers,
      base::OnceCallback<void(std::shared_ptr<brillo::http::Response>)>
          request_success_callback,
      base::OnceCallback<void(Result)> request_error_callback);

  // Stop the current HttpRequest.  No callback is called as a side
  // effect of this function.
  virtual void Stop();

  virtual const std::string& logging_tag() const { return logging_tag_; }

 private:
  friend class HttpRequestTest;

  // Time to wait for HTTP request.
  static constexpr base::TimeDelta kRequestTimeout = base::Seconds(10);

  void GetDNSResult(const base::expected<net_base::IPAddress, Error>& address);
  void StartRequest();
  void SuccessCallback(brillo::http::RequestID request_id,
                       std::unique_ptr<brillo::http::Response> response);
  void ErrorCallback(brillo::http::RequestID request_id,
                     const brillo::Error* error);
  void SendStatus(Result result);

  std::string logging_tag_;
  net_base::IPFamily ip_family_;
  std::vector<net_base::IPAddress> dns_list_;

  base::WeakPtrFactory<HttpRequest> weak_ptr_factory_;
  DnsClient::ClientCallback dns_client_callback_;
  base::OnceCallback<void(Result)> request_error_callback_;
  base::OnceCallback<void(std::shared_ptr<brillo::http::Response>)>
      request_success_callback_;
  std::unique_ptr<DnsClient> dns_client_;
  std::shared_ptr<brillo::http::Transport> transport_;
  brillo::http::RequestID request_id_;
  HttpUrl url_;
  brillo::http::HeaderList headers_;
  bool is_running_;
};

}  // namespace shill

#endif  // SHILL_HTTP_REQUEST_H_
