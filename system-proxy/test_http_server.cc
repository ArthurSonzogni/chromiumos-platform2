// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "system-proxy/test_http_server.h"

#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <memory>

#include <base/functional/bind.h>
#include <base/functional/callback_helpers.h>
#include <base/logging.h>
#include <base/strings/stringprintf.h>
#include <chromeos/net-base/socket.h>

namespace {
constexpr int kMaxConn = 10;

const std::string_view kConnectionEstablished =
    "HTTP/1.1 200 Connection established\r\n\r\n";

const std::string_view kProxyAuthenticationRequiredBasic =
    "HTTP/1.1 407 Proxy Authentication Required\r\n"
    "Proxy-Authenticate: Basic realm=\"My Proxy\"\r\n"
    "\r\n";

const std::string_view kProxyAuthenticationRequiredNegotiate =
    "HTTP/1.1 407 Proxy Authentication Required\r\n"
    "Proxy-Authenticate: Negotiate realm=\"My Proxy\"\r\n"
    "\r\n";

const std::string_view kHttpBadGateway =
    "HTTP/1.1 502 Bad Gateway\r\n\r\nBad gateway message from the server";

}  // namespace
namespace system_proxy {

HttpTestServer::HttpTestServer()
    : base::SimpleThread("HttpTestServer"),
      listening_addr_(127, 0, 0, 1),  // INADDR_LOOPBACK
      listening_port_(0) {}

HttpTestServer::~HttpTestServer() {
  if (!HasBeenStarted()) {
    return;
  }

  // Close the listening socket.
  listening_socket_.reset();

  Join();
}

void HttpTestServer::Run() {
  struct sockaddr_storage client_src = {};
  socklen_t sockaddr_len = sizeof(client_src);
  while (!expected_responses_.empty()) {
    if (std::unique_ptr<net_base::Socket> client_conn =
            listening_socket_->Accept((struct sockaddr*)&client_src,
                                      &sockaddr_len)) {
      std::string_view server_reply =
          GetConnectReplyString(expected_responses_.front());
      expected_responses_.pop();
      client_conn->Send(server_reply);
    }
  }
}

void HttpTestServer::BeforeStart() {
  listening_socket_ = net_base::Socket::Create(AF_INET, SOCK_STREAM);
  if (!listening_socket_) {
    PLOG(ERROR) << "Cannot create the listening socket" << std::endl;
    return;
  }

  struct sockaddr_in addr = {0};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(listening_port_);
  addr.sin_addr = listening_addr_.ToInAddr();
  if (!listening_socket_->Bind((const struct sockaddr*)&addr, sizeof(addr))) {
    PLOG(ERROR) << "Cannot bind source socket" << std::endl;
    return;
  }

  if (!listening_socket_->Listen(kMaxConn)) {
    PLOG(ERROR) << "Cannot listen on source socket." << std::endl;
    return;
  }

  socklen_t len = sizeof(addr);
  if (!listening_socket_->GetSockName((struct sockaddr*)&addr, &len)) {
    PLOG(ERROR) << "Cannot get the listening port " << std::endl;
    return;
  }
  listening_port_ = ntohs(addr.sin_port);
}

std::string HttpTestServer::GetUrl() {
  return base::StringPrintf("http://%s:%d", listening_addr_.ToString().c_str(),
                            listening_port_);
}

void HttpTestServer::AddHttpConnectReply(HttpConnectReply reply) {
  expected_responses_.push(reply);
}

std::string_view HttpTestServer::GetConnectReplyString(HttpConnectReply reply) {
  switch (reply) {
    case HttpConnectReply::kOk:
      return kConnectionEstablished;
    case HttpConnectReply::kAuthRequiredBasic:
      return kProxyAuthenticationRequiredBasic;
    case HttpConnectReply::kAuthRequiredKerberos:
      return kProxyAuthenticationRequiredNegotiate;
    case HttpConnectReply::kBadGateway:
      return kHttpBadGateway;
    default:
      return kConnectionEstablished;
  }
}

}  // namespace  system_proxy
