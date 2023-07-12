// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net-base/byte_utils.h"

#include <string.h>

#include <base/sys_byteorder.h>

namespace net_base::byte_utils {

std::vector<uint8_t> StringToCStringBytes(std::string_view str) {
  const size_t len = strnlen(str.data(), str.size());
  std::vector<uint8_t> bytes{
      reinterpret_cast<const uint8_t*>(str.data()),
      reinterpret_cast<const uint8_t*>(str.data()) + len};
  bytes.push_back(0);  // Add a null character at the end.
  return bytes;
}

std::string StringFromCStringBytes(base::span<const uint8_t> bytes) {
  const size_t len =
      strnlen(reinterpret_cast<const char*>(bytes.data()), bytes.size());
  return {reinterpret_cast<const char*>(bytes.data()), len};
}

std::vector<uint8_t> ByteStringToBytes(std::string_view bytes) {
  return {reinterpret_cast<const uint8_t*>(bytes.data()),
          reinterpret_cast<const uint8_t*>(bytes.data() + bytes.size())};
}

std::string ByteStringFromBytes(base::span<const uint8_t> bytes) {
  return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

}  // namespace net_base::byte_utils
