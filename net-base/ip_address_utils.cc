// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net-base/ip_address_utils.h"

#include <arpa/inet.h>

#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>

namespace net_base {

std::optional<std::pair<std::string_view, int>> SplitCIDRString(
    std::string_view address_string) {
  const auto address_parts = base::SplitStringPiece(
      address_string, "/", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);
  if (address_parts.size() != 2) {
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

  if (src.length() >= kMaxAddrLength) {
    return 0;
  }

  char src_buf[kMaxAddrLength];
  memcpy(src_buf, src.data(), src.length());
  src_buf[src.length()] = '\0';
  return inet_pton(af, src_buf, dst);
}

}  // namespace net_base
