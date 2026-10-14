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

// Simple HTTP/HTTPS URL parsing class.
//
// Parsing and formatting rules:
// - Input must be valid UTF-8 (`base::IsStringUTF8`) and contain no ASCII
//   whitespace. Because `HttpUrl` strings are exposed over D-Bus (e.g.
//   `captive_portal_uri` from DHCP option 114 or RFC 8910 RA option 37),
//   non-UTF-8 strings would cause `libdbus` to crash the daemon.
// - Only `http://` (default port 80) and `https://` (default port 443) are
//   accepted.
// - The host ends at the first `/`, `#`, or `?` (`kDelimiters`), or end of
//   string, and may include `:port` where `port` is all ASCII digits in
//   `[1, 65535]`.
// - `path()` is always normalized to start with `'/'` (e.g.
//   `"http://example.com"` -> `path() == "/"` and
//   `"http://example.com?foo"` -> `path() == "/?foo"`).
// - `ToString()` omits the port if it matches the protocol's default port,
//   omits `path_` when `path_ == "/"`, and strips the synthetic leading `'/'`
//   when `path_` starts with `"/?` or `"/#"` so query/fragment-only URLs
//   round-trip cleanly. Returns `"<invalid>"` if `protocol_ == kUnknown`.
//
// Note: External consumers (`shill` portal detection, DHCP option 114, RFC 8910
// RA captive portal) rely on `HttpUrl` parsing strictness; verify all external
// call sites when tightening or relaxing validation.
class BRILLO_EXPORT HttpUrl {
 public:
  enum class Protocol { kUnknown, kHttp, kHttps };

  static const int kDefaultHttpPort;
  static const int kDefaultHttpsPort;

  HttpUrl();
  ~HttpUrl();

  // Parses a URL from |url_string|. Rejects URLs containing any ASCII
  // whitespace, and requires the port (if present) to be all ASCII digits in
  // the range 1..65535.
  static std::optional<HttpUrl> CreateFromString(std::string_view url_string);

  // Parse a URL from |url_string|. Rejects URLs containing any ASCII
  // whitespace, and requires the port (if present) to be all ASCII digits in
  // the range 1..65535.
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
