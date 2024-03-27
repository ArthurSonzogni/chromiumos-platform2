// Copyright 2017 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/mac_address_generator.h"

#include <base/rand_util.h>
#include <net-base/mac_address.h>

namespace patchpanel {

net_base::MacAddress MacAddressGenerator::Generate() {
  net_base::MacAddress addr;
  do {
    addr = net_base::MacAddress::CreateRandom();
  } while (addrs_.find(addr) != addrs_.end() ||
           memcmp(addr.data(), kStableBaseAddr.data(), 5) == 0);

  addrs_.insert(addr);
  return addr;
}

net_base::MacAddress MacAddressGenerator::GetStable(uint32_t id) const {
  net_base::MacAddress::DataType addr = kStableBaseAddr;
  addr[5] = static_cast<uint8_t>(id);
  return net_base::MacAddress(addr);
}

}  // namespace patchpanel
