// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/vpn/ipsec_connection.h"

#include <utility>

namespace shill {

IPsecConnection::IPsecConnection(std::unique_ptr<Config> config,
                                 std::unique_ptr<Callbacks> callbacks,
                                 EventDispatcher* dispatcher)
    : VPNConnection(std::move(callbacks), dispatcher),
      config_(std::move(config)) {}

void IPsecConnection::OnConnect() {
  // TODO(b/165170125): Write strongswan.conf, start charon, and then wait for
  // vici socket ready.
}

void IPsecConnection::OnDisconnect() {
  // TODO(b/165170125): Implement OnDisconnect().
}

}  // namespace shill
