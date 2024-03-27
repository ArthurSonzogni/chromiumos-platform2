// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/mac_address.h"

#include <optional>
#include <string>

#include <crypto/random.h>
#include <net-base/mac_address.h>

#include "shill/store/store_interface.h"

namespace shill {

// static
MACAddress MACAddress::CreateRandom() {
  return MACAddress(net_base::MacAddress::CreateRandom(), kNotExpiring);
}

MACAddress::MACAddress() = default;
MACAddress::MACAddress(net_base::MacAddress address, base::Time expiration_time)
    : address_(address), expiration_time_(expiration_time) {}

void MACAddress::Clear() {
  address_ = std::nullopt;
  expiration_time_ = kNotExpiring;
}

bool MACAddress::IsExpired(base::Time now) const {
  // We assume == is still not expired to be on the safe side.
  return (expiration_time_ != kNotExpiring) && (now > expiration_time_);
}

bool MACAddress::Load(const StoreInterface* storage, const std::string& id) {
  std::string mac_str;
  if (!storage->GetString(id, kStorageMACAddress, &mac_str)) {
    return false;
  }

  const std::optional<net_base::MacAddress> address =
      net_base::MacAddress::CreateFromString(mac_str);
  if (!address.has_value()) {
    return false;
  }
  address_ = *address;

  uint64_t expiration_time;
  if (storage->GetUint64(id, kStorageMACAddressExpiry, &expiration_time)) {
    expiration_time_ = base::Time::FromDeltaSinceWindowsEpoch(
        base::Microseconds(expiration_time));
  }
  return true;
}

bool MACAddress::Save(StoreInterface* storage, const std::string& id) const {
  if (!address_.has_value()) {
    return false;
  }
  storage->SetString(id, kStorageMACAddress, ToString());
  storage->SetUint64(
      id, kStorageMACAddressExpiry,
      expiration_time_.ToDeltaSinceWindowsEpoch().InMicroseconds());
  return true;
}

std::string MACAddress::ToString() const {
  if (!address_.has_value()) {
    return "<UNSET>";
  }
  return address_->ToString();
}

std::ostream& operator<<(std::ostream& os, const MACAddress& addr) {
  os << addr.ToString();
  return os;
}

}  // namespace shill
