// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cros-disks/uri.h"

#include <base/logging.h>

namespace cros_disks {
namespace {

const std::string_view kUriDelimiter = "://";

}  // namespace

Uri::Uri(const std::string_view scheme, const std::string_view path)
    : scheme_(scheme), path_(path) {}

std::string Uri::value() const {
  std::string s;

  if (valid()) {
    s.reserve(scheme_.size() + kUriDelimiter.size() + path_.size());
    s += scheme_;
    s.append(kUriDelimiter.data(), kUriDelimiter.size());
    s += path_;
  }

  return s;
}

Uri Uri::Parse(const std::string_view s) {
  // Look for URI scheme delimiter.
  const size_t pos = s.find(kUriDelimiter);
  if (pos == std::string_view::npos || pos == 0) {
    return {};
  }

  // Extract scheme part.
  const std::string_view scheme = s.substr(0, pos);

  // Check scheme validity (see RFC 3986, section 3.1).
  if (!isalpha(scheme[0])) {
    return {};
  }

  for (const char c : scheme.substr(1)) {
    if (!isalnum(c) && c != '-' && c != '+' && c != '.') {
      return {};
    }
  }

  // Scheme is deemed valid.
  return {scheme, s.substr(pos + kUriDelimiter.size())};
}

}  // namespace cros_disks
