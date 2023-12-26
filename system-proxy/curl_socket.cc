// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "system-proxy/curl_socket.h"

#include <memory>
#include <utility>

#include <base/files/scoped_file.h>
#include <base/logging.h>
#include <curl/easy.h>

namespace system_proxy {

std::unique_ptr<CurlSocket> CurlSocket::CreateFromCURLHandle(CURL* easyhandle) {
  // Extract the socket from the curl handle.
  curl_socket_t curl_socket = -1;
  const CURLcode res =
      curl_easy_getinfo(easyhandle, CURLINFO_ACTIVESOCKET, &curl_socket);
  if (res != CURLE_OK) {
    LOG(ERROR) << "Failed to get socket from curl with error: "
               << curl_easy_strerror(res);
    return nullptr;
  }

  // Duplicate the fd because the original fd is owned by the curl handle.
  base::ScopedFD duped_socket(dup(curl_socket));
  if (!duped_socket.is_valid()) {
    PLOG(ERROR) << "Failed to duplicate the curl socket";
    return nullptr;
  }

  return std::make_unique<CurlSocket>(
      std::move(duped_socket),
      ScopedCurlEasyhandle(easyhandle, FreeCurlEasyhandle()));
}

CurlSocket::CurlSocket(base::ScopedFD fd, ScopedCurlEasyhandle curl_easyhandle)
    : patchpanel::Socket(std::move(fd)),
      curl_easyhandle_(std::move(curl_easyhandle)) {}

CurlSocket::~CurlSocket() = default;

}  // namespace system_proxy
