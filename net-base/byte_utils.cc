// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net-base/byte_utils.h"

#include <algorithm>

namespace net_base::byte_utils {

std::vector<uint8_t> StringToCStringBytes(std::string_view str) {
  const size_t len = std::min(str.find('\0'), str.size());
  std::vector<uint8_t> bytes{
      reinterpret_cast<const uint8_t*>(str.data()),
      reinterpret_cast<const uint8_t*>(str.data()) + len};
  bytes.push_back(0);  // Add a null character at the end.
  return bytes;
}

std::string StringFromCStringBytes(base::span<const uint8_t> bytes) {
  const auto end = std::find(bytes.begin(), bytes.end(), 0);
  return {reinterpret_cast<const char*>(bytes.data()),
          static_cast<size_t>(end - bytes.begin())};
}

std::vector<uint8_t> ByteStringToBytes(std::string_view bytes) {
  return {reinterpret_cast<const uint8_t*>(bytes.data()),
          reinterpret_cast<const uint8_t*>(bytes.data() + bytes.size())};
}

base::span<const uint8_t> ByteStringAsBytes(std::string_view bytes) {
  return {reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size()};
}

std::string ByteStringFromBytes(base::span<const uint8_t> bytes) {
  return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

}  // namespace net_base::byte_utils
