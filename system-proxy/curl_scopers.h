// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SYSTEM_PROXY_CURL_SCOPERS_H_
#define SYSTEM_PROXY_CURL_SCOPERS_H_

#include <memory>

#include <base/files/scoped_file.h>
#include <curl/curl.h>
#include <curl/easy.h>
#include <net-base/socket.h>
#include <net-base/socket_forwarder.h>

namespace system_proxy {

// Frees the resources allocated by curl_easy_init.
struct FreeCurlEasyhandle {
  void operator()(CURL* ptr) const { curl_easy_cleanup(ptr); }
};

// The destructor needs to call curl_easy_cleanup instead of
// operator delete.
typedef std::unique_ptr<CURL, FreeCurlEasyhandle> ScopedCurlEasyhandle;

// CurlForwarder wraps resources needed to keep a forwarding connection
// using a socket opened by curl alive. On destruction, all sockets
// and the curl handle will be cleaned up.
class CurlForwarder {
 public:
  static std::unique_ptr<CurlForwarder> Create(
      std::unique_ptr<net_base::Socket> client_socket,
      std::unique_ptr<net_base::Socket> server_socket,
      ScopedCurlEasyhandle server_handle);
  ~CurlForwarder();

  bool IsFinished() const;

 private:
  CurlForwarder(std::unique_ptr<net_base::SocketForwarder> forwarder,
                ScopedCurlEasyhandle server_handle);

  std::unique_ptr<net_base::SocketForwarder> forwarder_;
  ScopedCurlEasyhandle server_handle_;
};

}  // namespace system_proxy

#endif  // SYSTEM_PROXY_CURL_SCOPERS_H_
