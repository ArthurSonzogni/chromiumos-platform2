// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_NETWORK_CAPPORT_CLIENT_H_
#define SHILL_NETWORK_CAPPORT_CLIENT_H_

#include <memory>
#include <optional>
#include <string>

#include <base/functional/callback.h>

#include "shill/network/capport_proxy.h"

namespace shill {

// The client of CapportProxy.
class CapportClient {
 public:
  enum class State {
    // Failed to get the valid information from CAPPORT server.
    kFailed,
    // The portal is closed.
    kClosed,
    // The portal is open.
    kOpen,
  };

  // The result of the CapportClient that is returned to CapportClient's caller.
  struct Result {
    State state;
    std::optional<net_base::HttpUrl> user_portal_url;
    std::optional<net_base::HttpUrl> venue_info_url;

    bool operator==(const Result&) const;
    bool operator!=(const Result&) const;
  };

  // The callback type that is used to return the result back to the caller of
  // CapportClient asynchronously.
  using ResultCallback = base::RepeatingCallback<void(const Result&)>;

  // Constructs the instance. |proxy| must be a valid instance.
  // |result_callback| is called after QueryCapport() finishes.
  // |logging_tag| is the tag that will be printed at every logging.
  // Note that |result_callback| won't be called after the CapportClient
  // instance is destroyed.
  CapportClient(std::unique_ptr<CapportProxy> proxy,
                ResultCallback result_callback,
                std::string_view logging_tag = "");
  ~CapportClient();

  // Queries the CAPPORT server via |proxy_|. After this method is called,
  // |result_callback_| is guaranteed to be called at least once. But it's not
  // guaranteed that each QueryCapport() call has one corresponding
  // |result_callback_| call. For example, if QueryCapport() is called twice
  // immediately, |result_callback_| will be called at least once after that.
  void QueryCapport();

 private:
  // Called when |proxy_| has received the status from the CAPPORT server.
  void OnStatusReceived(std::optional<CapportStatus> status);

  std::unique_ptr<CapportProxy> proxy_;
  ResultCallback result_callback_;
  std::string logging_tag_;
};

}  // namespace shill
#endif  // SHILL_NETWORK_CAPPORT_CLIENT_H_
