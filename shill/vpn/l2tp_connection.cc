// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/vpn/l2tp_connection.h"

#include <utility>

namespace shill {

L2TPConnection::L2TPConnection(std::unique_ptr<Config> config,
                               std::unique_ptr<Callbacks> callbacks,
                               EventDispatcher* dispatcher)
    : VPNConnection(std::move(callbacks), dispatcher),
      config_(std::move(config)) {}

L2TPConnection::~L2TPConnection() {
  if (state() == State::kIdle || state() == State::kStopped) {
    return;
  }

  // This is unexpected but cannot be fully avoided. Call OnDisconnect() to make
  // sure resources are released.
  LOG(WARNING) << "Destructor called but the current state is " << state();
  OnDisconnect();
}

void L2TPConnection::OnConnect() {
  // TODO(b/165170125): Implement OnConnect().
}

void L2TPConnection::OnDisconnect() {
  // TODO(b/165170125): Implement OnConnect().
}

}  // namespace shill
