// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_BASE_HTTP_URL_H_
#define NET_BASE_HTTP_URL_H_

#include <optional>
#include <string>
#include <string_view>

#include <brillo/brillo_export.h>

namespace net_base {

// Simple URL parsing class.
class BRILLO_EXPORT HttpUrl {
 public:
  enum class Protocol { kUnknown, kHttp, kHttps };

  static const int kDefaultHttpPort;
  static const int kDefaultHttpsPort;

  HttpUrl();
  ~HttpUrl();

  static std::optional<HttpUrl> CreateFromString(std::string_view url_string);

  // Parse a URL from |url_string|.
  bool ParseFromString(std::string_view url_string);

  const std::string& host() const { return host_; }
  const std::string& path() const { return path_; }
  int port() const { return port_; }
  Protocol protocol() const { return protocol_; }
  std::string ToString() const;

  bool operator==(const HttpUrl& rhs) const;
  bool operator!=(const HttpUrl& rhs) const;

 private:
  std::string host_;
  std::string path_;
  int port_;
  Protocol protocol_;
};

}  // namespace net_base

#endif  // NET_BASE_HTTP_URL_H_
