// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/http_url.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>

namespace shill {

namespace {

constexpr char kDelimiters[] = " /#?";
constexpr char kPortSeparator = ':';
constexpr char kPrefixHttp[] = "http://";
constexpr char kPrefixHttps[] = "https://";

}  //  namespace

const int HttpUrl::kDefaultHttpPort = 80;
const int HttpUrl::kDefaultHttpsPort = 443;

HttpUrl::HttpUrl() : port_(kDefaultHttpPort), protocol_(Protocol::kHttp) {}

HttpUrl::~HttpUrl() = default;

// static
std::optional<HttpUrl> HttpUrl::CreateFromString(std::string_view url_string) {
  HttpUrl url;
  if (!url.ParseFromString(url_string)) {
    return std::nullopt;
  }
  return url;
}

bool HttpUrl::ParseFromString(std::string_view url_string) {
  Protocol protocol = Protocol::kUnknown;
  size_t host_start = 0;
  int port = 0;
  const std::string http_url_prefix(kPrefixHttp);
  const std::string https_url_prefix(kPrefixHttps);
  if (url_string.substr(0, http_url_prefix.length()) == http_url_prefix) {
    host_start = http_url_prefix.length();
    port = kDefaultHttpPort;
    protocol = Protocol::kHttp;
  } else if (url_string.substr(0, https_url_prefix.length()) ==
             https_url_prefix) {
    host_start = https_url_prefix.length();
    port = kDefaultHttpsPort;
    protocol = Protocol::kHttps;
  } else {
    return false;
  }

  size_t host_end = url_string.find_first_of(kDelimiters, host_start);
  if (host_end == std::string::npos) {
    host_end = url_string.length();
  }
  const auto host_parts = base::SplitString(
      url_string.substr(host_start, host_end - host_start),
      std::string{kPortSeparator}, base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);

  if (host_parts.empty() || host_parts[0].empty() || host_parts.size() > 2) {
    return false;
  }

  if (host_parts.size() == 2) {
    if (!base::StringToInt(host_parts[1], &port)) {
      return false;
    }
  }

  protocol_ = protocol;
  host_ = host_parts[0];
  port_ = port;
  path_ = url_string.substr(host_end);
  if (path_.empty() || path_[0] != '/') {
    path_ = "/" + path_;
  }

  return true;
}

std::string HttpUrl::ToString() const {
  int port = port_;
  std::string url_string;
  switch (protocol_) {
    case Protocol::kUnknown:
      return "<invalid>";
    case Protocol::kHttp:
      url_string = kPrefixHttp;
      if (port == kDefaultHttpPort) {
        port = 0;
      }
      break;
    case Protocol::kHttps:
      url_string = kPrefixHttps;
      if (port == kDefaultHttpsPort) {
        port = 0;
      }
      break;
  }
  url_string += host_;
  if (port != 0) {
    url_string += ":" + std::to_string(port);
  }
  if (path_ != "/") {
    if (base::StartsWith(path_, "/?")) {
      url_string += path_.substr(1);
    } else {
      url_string += path_;
    }
  }
  return url_string;
}

bool HttpUrl::operator==(const HttpUrl& rhs) const {
  return host_ == rhs.host_ && path_ == rhs.path_ && port_ == rhs.port_ &&
         protocol_ == rhs.protocol_;
}

bool HttpUrl::operator!=(const HttpUrl& rhs) const {
  return !(*this == rhs);
}

}  // namespace shill
