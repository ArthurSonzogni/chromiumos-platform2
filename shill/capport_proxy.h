// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_CAPPORT_PROXY_H_
#define SHILL_CAPPORT_PROXY_H_

#include <compare>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include <base/functional/callback.h>
#include <base/time/time.h>
#include <brillo/http/http_request.h>
#include <brillo/http/http_transport.h>

#include "shill/http_url.h"
#include "shill/mockable.h"

namespace shill {

// The returned status from CAPPORT API, defined at RFC8908.
struct CapportStatus {
  static std::optional<CapportStatus> ParseFromJson(std::string_view json_str);

  bool operator==(const CapportStatus& rhs) const = default;
  bool operator!=(const CapportStatus& rhs) const = default;

  bool is_captive;
  std::optional<HttpUrl> user_portal_url;
  std::optional<HttpUrl> venue_info_url;
  std::optional<bool> can_extend_session;
  std::optional<base::TimeDelta> seconds_remaining;
  std::optional<int> bytes_remaining;
};

// The proxy of the CAPPORT API server.
class CapportProxy {
 public:
  using StatusCallback = base::OnceCallback<void(std::optional<CapportStatus>)>;
  static constexpr base::TimeDelta kDefaultTimeout = base::Seconds(5);

  // Creates a CapportProxy instance. The HTTP requests to the CAPPORT server
  // will go through |interface|. |api_url| is the URL of the CAPPORT server
  // discovered with RFC8910. The HTTP request will be send through
  // |http_transport| instance. Note that |api_url| must be HTTPS URL.
  static std::unique_ptr<CapportProxy> Create(
      std::string_view interface,
      std::string_view api_url,
      std::shared_ptr<brillo::http::Transport> http_transport =
          brillo::http::Transport::CreateDefault(),
      base::TimeDelta transport_timeout = kDefaultTimeout);

  CapportProxy(std::string_view api_url,
               std::shared_ptr<brillo::http::Transport> http_transport,
               std::string_view logging_tag = "");
  virtual ~CapportProxy();

  // It's non-copyable and non-movable.
  CapportProxy(const CapportProxy&) = delete;
  CapportProxy& operator=(const CapportProxy&) = delete;
  CapportProxy(CapportProxy&&) = delete;
  CapportProxy& operator=(CapportProxy&&) = delete;

  // Queries the CAPPORT server. The |callback| will be called with a valid
  // CapportStatus when the response is received from the CAPPORT server
  // successfully, or with std::nullopt when any error occurs.
  // Note that the caller should not call this method when IsRunning() is true.
  mockable void SendRequest(StatusCallback callback);

  // Returns true if the previous request has not been finished.
  mockable bool IsRunning() const;

 private:
  void OnRequestSuccess(brillo::http::RequestID request_id,
                        std::unique_ptr<brillo::http::Response> response);
  void OnRequestError(brillo::http::RequestID request_id,
                      const brillo::Error* error);

  // The URL of the CAPPORT server.
  std::string api_url_;
  // The HTTP transport used to send request to CAPPORT server.
  std::shared_ptr<brillo::http::Transport> http_transport_;
  // The tag that will be printed at every logging.
  std::string logging_tag_;

  // The request to the CAPPORT server, only has value when there is pending
  // request.
  std::optional<brillo::http::Request> http_request_;
  // The callback of the request, only has value when there is pending
  // request.
  StatusCallback callback_;
};

}  // namespace shill
#endif  // SHILL_CAPPORT_PROXY_H_
