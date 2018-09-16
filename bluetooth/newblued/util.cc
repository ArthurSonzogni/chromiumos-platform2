// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "bluetooth/newblued/util.h"

#include <newblue/bt.h>

#include <algorithm>
#include <regex>  // NOLINT(build/c++11)

#include <base/stl_util.h>
#include <base/strings/string_split.h>
#include <base/strings/string_number_conversions.h>

namespace {

uint64_t GetNumFromLE(const uint8_t* buf, uint8_t bits) {
  uint64_t val = 0;
  uint8_t bytes = bits / 8;

  CHECK(buf);

  buf += bytes;

  while (bytes--)
    val = (val << 8) | *--buf;

  return val;
}

}  // namespace

namespace bluetooth {

// Turns the content of |buf| into a uint16_t in host order. This should be used
// when reading the little-endian data from Bluetooth packet.
uint16_t GetNumFromLE16(const uint8_t* buf) {
  return static_cast<uint16_t>(GetNumFromLE(buf, 16));
}
// Turns the content of |buf| into a uint32_t in host order. This should be used
// when reading the little-endian data from Bluetooth packet.
uint32_t GetNumFromLE24(const uint8_t* buf) {
  return static_cast<uint32_t>(GetNumFromLE(buf, 24));
}

// Reverses the content of |buf| and returns bytes in big-endian order. This
// should be used when reading the little-endian data from Bluetooth packet.
std::vector<uint8_t> GetBytesFromLE(const uint8_t* buf, size_t buf_len) {
  std::vector<uint8_t> ret;

  CHECK(buf);

  if (!buf_len)
    return ret;

  ret.assign(buf, buf + buf_len);
  std::reverse(ret.begin(), ret.end());
  return ret;
}

UniqueId GetNextId() {
  static UniqueId next_id = 1;
  uint64_t id = next_id++;
  if (id)
    return id;
  next_id -= 1;
  LOG(ERROR) << "Run out of unique IDs";
  return 0;
}

bool ConvertToBtAddr(bool is_random_address,
                     const std::string& address,
                     struct bt_addr* result) {
  CHECK(result);

  std::vector<std::string> tokens = base::SplitString(
      address, ":", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL);
  if (tokens.size() != BT_MAC_LEN)
    return false;

  uint8_t addr[BT_MAC_LEN];
  uint8_t* ptr = addr + BT_MAC_LEN;
  for (const auto& token : tokens) {
    uint32_t value;
    if (token.size() != 2 || !base::HexStringToUInt(token, &value))
      return false;
    *(--ptr) = static_cast<uint8_t>(value);
  }

  memcpy(result->addr, addr, BT_MAC_LEN);
  result->type =
      is_random_address ? BT_ADDR_TYPE_LE_RANDOM : BT_ADDR_TYPE_LE_PUBLIC;
  return true;
}

std::string ConvertDeviceObjectPathToAddress(const std::string& path) {
  std::string address;
  std::regex rgx("dev_([0-9a-fA-F]{2}_){5}[0-9a-fA-F]{2}$");
  std::smatch match;

  if (std::regex_search(path, match, rgx)) {
    address = std::string(match[0]).substr(4);
    std::replace(address.begin(), address.end(), '_', ':');
  }
  return address;
}

}  // namespace bluetooth
