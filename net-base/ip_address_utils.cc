// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net-base/ip_address_utils.h"

#include <arpa/inet.h>

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>

namespace net_base {

std::optional<std::pair<std::string_view, int>> SplitCIDRString(
    std::string_view address_string) {
  const auto address_parts = base::SplitStringPiece(
      address_string, "/", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL);
  if (address_parts.size() != 2) {
    return std::nullopt;
  }

  if (!std::all_of(address_parts[1].begin(), address_parts[1].end(),
                   [](char c) { return base::IsAsciiDigit(c); })) {
    return std::nullopt;
  }

  int prefix_length;
  if (!base::StringToInt(address_parts[1], &prefix_length)) {
    return std::nullopt;
  }
  return std::make_pair(address_parts[0], prefix_length);
}

int inet_pton_string_view(int af, std::string_view src, void* dst) {
  constexpr static size_t kMaxAddrLength = INET6_ADDRSTRLEN;

  if (src.empty() || src.length() > kMaxAddrLength) {
    return 0;
  }
  const size_t null_pos = src.find('\0');
  if (null_pos != std::string_view::npos && null_pos < src.length() - 1) {
    return 0;
  }

  char src_buf[kMaxAddrLength + 1];
  memcpy(src_buf, src.data(), src.length());
  src_buf[src.length()] = '\0';
  return inet_pton(af, src_buf, dst);
}

}  // namespace net_base
