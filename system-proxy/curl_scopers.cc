// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "system-proxy/curl_scopers.h"

#include <memory>
#include <string>
#include <utility>

#include <base/files/scoped_file.h>
#include <base/logging.h>
#include <base/strings/stringprintf.h>

namespace system_proxy {

CurlForwarder::CurlForwarder(
    std::unique_ptr<net_base::SocketForwarder> forwarder,
    ScopedCurlEasyhandle server_handle)
    : forwarder_(std::move(forwarder)),
      server_handle_(std::move(server_handle)) {}

CurlForwarder::~CurlForwarder() = default;

// static
std::unique_ptr<CurlForwarder> CurlForwarder::Create(
    std::unique_ptr<net_base::Socket> client_socket,
    std::unique_ptr<net_base::Socket> server_socket,
    ScopedCurlEasyhandle server_handle) {
  std::string name =
      base::StringPrintf("%d-%d", client_socket->Get(), server_socket->Get());
  auto fwd = std::make_unique<net_base::SocketForwarder>(
      name, std::move(client_socket), std::move(server_socket));
  fwd->Start();

  return std::unique_ptr<CurlForwarder>(
      new CurlForwarder(std::move(fwd), std::move(server_handle)));
}

bool CurlForwarder::IsFinished() const {
  return forwarder_->HasBeenStarted() && !forwarder_->IsRunning();
}

}  // namespace system_proxy
