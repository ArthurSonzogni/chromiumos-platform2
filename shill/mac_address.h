// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SHILL_MAC_ADDRESS_H_
#define SHILL_MAC_ADDRESS_H_

#include <string>
#include <base/time/time.h>
#include <net-base/mac_address.h>

namespace shill {

class StoreInterface;

// MACAddress class encapsulates MAC address, providing means for
// keeping it, accessing, setting and randomizing.
class MACAddress {
 public:
  // Multicast address bit.
  static constexpr uint8_t kMulicastMacBit = 0x01;
  // Locally administered bit.
  static constexpr uint8_t kLocallyAdministratedMacBit = 0x02;
  // Default expiration time for RandomizedMACAddress.
  static constexpr base::TimeDelta kDefaultExpirationTime = base::Hours(24);
  // Set expiration time to this constant to disable expiration.
  static constexpr base::Time kNotExpiring = base::Time();

  // Creates a MACAddress instance with a randomized address.
  static MACAddress CreateRandom();

  MACAddress();
  MACAddress(net_base::MacAddress address, base::Time expiration_time);

  // Clears the address and resets expiration time.
  void Clear();

  std::optional<net_base::MacAddress> address() const { return address_; }

  // Returns is true if address has expired.
  bool IsExpired(base::Time now) const;

  // Returns true if the address has a chance to expire.
  bool will_expire() const { return expiration_time_ != kNotExpiring; }

  // Loads MACAddress-related data from store.
  bool Load(const StoreInterface* storage, const std::string& id);

  // Saves MACAddress-related data to store.
  bool Save(StoreInterface* storage, const std::string& id) const;

  // Set the address, it's only used for testing.
  void set_address_for_test(net_base::MacAddress address) {
    address_ = address;
  }

  // Sets expiration time of the address.
  void set_expiration_time(base::Time when) { expiration_time_ = when; }

  // Returns address in aa:bb:cc:dd:ee:ff string notation.
  std::string ToString() const;

  // Convenient operator for logging.
  friend std::ostream& operator<<(std::ostream& os, const MACAddress& addr);

  friend bool operator==(const MACAddress& a1, const MACAddress& a2) = default;

 private:
  static constexpr char kStorageMACAddress[] = "WiFi.MACAddress";
  static constexpr char kStorageMACAddressExpiry[] = "WiFi.MACAddress.Expiry";

  std::optional<net_base::MacAddress> address_;
  base::Time expiration_time_ = kNotExpiring;
};

}  // namespace shill

#endif  // SHILL_MAC_ADDRESS_H_
