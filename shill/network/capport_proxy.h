// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_NETWORK_CAPPORT_PROXY_H_
#define SHILL_NETWORK_CAPPORT_PROXY_H_

#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include <base/containers/span.h>
#include <base/functional/callback.h>
#include <base/memory/weak_ptr.h>
#include <base/time/time.h>
#include <brillo/http/http_request.h>
#include <brillo/http/http_transport.h>
#include <net-base/http_url.h>
#include <net-base/ip_address.h>

#include "shill/metrics.h"
#include "shill/mockable.h"

namespace shill {

// The returned status from CAPPORT API, defined at RFC8908.
struct CapportStatus {
  static std::optional<CapportStatus> ParseFromJson(std::string_view json_str);

  bool operator==(const CapportStatus& rhs) const = default;
  bool operator!=(const CapportStatus& rhs) const = default;

  bool is_captive;
  // The field should have value when |is_captive| is true.
  std::optional<net_base::HttpUrl> user_portal_url;
  std::optional<net_base::HttpUrl> venue_info_url;
  std::optional<bool> can_extend_session;
  std::optional<base::TimeDelta> seconds_remaining;
  std::optional<int> bytes_remaining;
};

// The proxy of the CAPPORT API server.
class CapportProxy {
 public:
  using StatusCallback =
      base::OnceCallback<void(const std::optional<CapportStatus>&)>;
  static constexpr base::TimeDelta kDefaultTimeout = base::Seconds(5);

  // Creates a CapportProxy instance. The HTTP requests to the CAPPORT server
  // will go through |interface| with the DNS list |dns_list|. |api_url| is the
  // URL of the CAPPORT server discovered with RFC8910. The HTTP request will be
  // send through |http_transport| instance. Note that |api_url| must be HTTPS
  // URL.
  static std::unique_ptr<CapportProxy> Create(
      Metrics* metrics,
      std::string_view interface,
      const net_base::HttpUrl& api_url,
      base::span<const net_base::IPAddress> dns_list,
      std::shared_ptr<brillo::http::Transport> http_transport =
          brillo::http::Transport::CreateDefault(),
      base::TimeDelta transport_timeout = kDefaultTimeout);

  CapportProxy(Metrics* metrics,
               const net_base::HttpUrl& api_url,
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
  // If the CapportProxy instance is destroyed before the response is received,
  // then |callback| will not be called.
  // Returns false and does nothing when IsRunning() is true.
  mockable bool SendRequest(StatusCallback callback);

  // Stops the current query if exists. The callback of previous request will
  // not be called.
  mockable void Stop();

  // Returns true if the previous request has not been finished.
  mockable bool IsRunning() const;

  // Exposes the private callback methods for testing.
  void OnRequestSuccessForTesting(
      brillo::http::RequestID request_id,
      std::unique_ptr<brillo::http::Response> response);
  void OnRequestErrorForTesting(brillo::http::RequestID request_id,
                                const brillo::Error* error);

 private:
  void OnRequestSuccess(brillo::http::RequestID request_id,
                        std::unique_ptr<brillo::http::Response> response);
  void OnRequestError(brillo::http::RequestID request_id,
                      const brillo::Error* error);

  Metrics* metrics_;

  // The URL of the CAPPORT server.
  net_base::HttpUrl api_url_;
  // The HTTP transport used to send request to CAPPORT server.
  std::shared_ptr<brillo::http::Transport> http_transport_;
  // The tag that will be printed at every logging.
  std::string logging_tag_;

  // The callback of the request, only has value when there is pending
  // request.
  StatusCallback callback_;

  // Indicates whether the CAPPORT server replies with a venue info URL.
  std::optional<bool> has_venue_info_url_ = std::nullopt;
  // Indicates whether the CAPPORT server replies with a seconds-remaining
  // field after is_captive has become false.
  std::optional<bool> has_seconds_remaining_ = std::nullopt;
  // Indicates whether the CAPPORT server replies with a bytes-remaining
  // field after is_captive has become false.
  std::optional<bool> has_bytes_remaining_ = std::nullopt;
  // Records the maximum number of the seconds_remaining field. The number
  // should be near to the time limit.
  std::optional<int> max_seconds_remaining_ = std::nullopt;

  base::WeakPtrFactory<CapportProxy> weak_ptr_factory_{this};
};

// The factory class of the CapportProxy, used to derive a mock factory to
// create mock CapportProxy instance at testing.
class CapportProxyFactory {
 public:
  CapportProxyFactory();
  virtual ~CapportProxyFactory();

  // The default factory method, calling CapportProxy::Create() method.
  virtual std::unique_ptr<CapportProxy> Create(
      Metrics* metrics,
      std::string_view interface,
      const net_base::HttpUrl& api_url,
      base::span<const net_base::IPAddress> dns_list,
      std::shared_ptr<brillo::http::Transport> http_transport =
          brillo::http::Transport::CreateDefault(),
      base::TimeDelta transport_timeout = CapportProxy::kDefaultTimeout);
};

}  // namespace shill
#endif  // SHILL_NETWORK_CAPPORT_PROXY_H_
