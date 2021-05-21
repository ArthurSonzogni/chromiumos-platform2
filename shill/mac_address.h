// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SHILL_MAC_ADDRESS_H_
#define SHILL_MAC_ADDRESS_H_

#include <array>
#include <string>

#include "linux/if_ether.h"

namespace shill {

// MACAddress class encapsulates MAC address, providing means for
// keeping it, accessing, setting and randomizing.
class MACAddress {
 public:
  // Multicast address bit.
  static constexpr uint8_t kMulicastMacBit = 0x01;
  // Locally administered bit.
  static constexpr uint8_t kLocallyAdministratedMacBit = 0x02;

  // It does not clear the address itself, but clears is_set flag.
  void Clear();

  bool is_set() const { return is_set_; }

  // Randomize address stored.
  void Randomize();

  // Set stored address to match particular string in aa:bb:cc:dd:ee:ff
  // notation. Returns false if given string was incorrect.
  bool Set(const std::string& str);

  // Returns address in aa:bb:cc:dd:ee:ff string notation.
  std::string ToString() const;

  // Convenient operator for logging.
  friend std::ostream& operator<<(std::ostream& os, const MACAddress& addr);

  MACAddress() = default;

 private:
  std::array<uint8_t, ETH_ALEN> address_;
  bool is_set_ = false;

  MACAddress(const MACAddress&) = delete;
  MACAddress(const MACAddress&&) = delete;
  MACAddress& operator=(const MACAddress&) = delete;
  MACAddress&& operator=(const MACAddress&&) = delete;
};

}  // namespace shill
#endif  // SHILL_MAC_ADDRESS_H_
