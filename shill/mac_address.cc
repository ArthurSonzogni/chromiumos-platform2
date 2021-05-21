// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <string>
#include <vector>

#include "crypto/random.h"
#include "shill/device.h"
#include "shill/mac_address.h"

namespace shill {

void MACAddress::Clear() {
  is_set_ = false;
}

void MACAddress::Randomize() {
  crypto::RandBytes(address_.data(), address_.size());

  address_[0] &= ~MACAddress::kMulicastMacBit;  // Set unicast address.
  address_[0] |= MACAddress::kLocallyAdministratedMacBit;
  is_set_ = true;
}

bool MACAddress::Set(const std::string& str) {
  const auto addr = Device::MakeHardwareAddressFromString(str);
  if (addr.size() != address_.size()) {
    return false;
  }
  std::copy_n(addr.begin(), address_.size(), address_.begin());
  is_set_ = true;
  return true;
}

std::string MACAddress::ToString() const {
  if (!is_set_) {
    return "<UNSET>";
  }
  const std::vector<uint8_t> addr(address_.begin(), address_.end());
  return Device::MakeStringFromHardwareAddress(addr);
}

std::ostream& operator<<(std::ostream& os, const MACAddress& addr) {
  os << addr.ToString();
  return os;
}

}  // namespace shill
