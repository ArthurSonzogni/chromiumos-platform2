// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_VPN_IPSEC_CONNECTION_H_
#define SHILL_VPN_IPSEC_CONNECTION_H_

#include <memory>

#include <base/callback.h>

#include "shill/vpn/vpn_connection.h"

namespace shill {

// IPsecConnection manages the IPsec connection by starting charon process and
// taking to it via swanctl.
// TODO(b/165170125): Document temporary files.
// TODO(b/165170125): Add unit tests.
class IPsecConnection : public VPNConnection {
 public:
  // TODO(b/165170125): Add fields.
  struct Config {};

  explicit IPsecConnection(std::unique_ptr<Config> config,
                           std::unique_ptr<Callbacks> callbacks,
                           EventDispatcher* dispatcher);
  ~IPsecConnection() = default;

 private:
  void OnConnect() override;
  void OnDisconnect() override;

  std::unique_ptr<Config> config_;

  base::WeakPtrFactory<IPsecConnection> weak_factory_{this};
};

}  // namespace shill

#endif  // SHILL_VPN_IPSEC_CONNECTION_H_
