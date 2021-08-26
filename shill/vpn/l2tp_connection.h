// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_VPN_L2TP_CONNECTION_H_
#define SHILL_VPN_L2TP_CONNECTION_H_

#include <memory>

#include "shill/event_dispatcher.h"
#include "shill/vpn/vpn_connection.h"

namespace shill {

// TODO(b/165170125): Document temporary files.
// TODO(b/165170125): Add unit tests.
class L2TPConnection : public VPNConnection {
 public:
  // TODO(b/165170125): Add fields for xl2tpd and pppd.
  struct Config {};

  explicit L2TPConnection(std::unique_ptr<Config> config,
                          std::unique_ptr<Callbacks> callbacks,
                          EventDispatcher* dispatcher);
  ~L2TPConnection();

 private:
  void OnConnect() override;
  void OnDisconnect() override;

  std::unique_ptr<Config> config_;

  base::WeakPtrFactory<L2TPConnection> weak_factory_{this};
};

}  // namespace shill

#endif  // SHILL_VPN_L2TP_CONNECTION_H_
