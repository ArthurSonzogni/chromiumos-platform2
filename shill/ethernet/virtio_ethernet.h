// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_ETHERNET_VIRTIO_ETHERNET_H_
#define SHILL_ETHERNET_VIRTIO_ETHERNET_H_

#include <optional>
#include <string>

#include <net-base/mac_address.h>

#include "shill/ethernet/ethernet.h"

namespace shill {

class VirtioEthernet : public Ethernet {
 public:
  VirtioEthernet(Manager* manager,
                 const std::string& link_name,
                 std::optional<net_base::MacAddress> mac_address,
                 int interface_index);
  VirtioEthernet(const VirtioEthernet&) = delete;
  VirtioEthernet& operator=(const VirtioEthernet&) = delete;

  void Start(EnabledStateChangedCallback callback) override;
};

}  // namespace shill

#endif  // SHILL_ETHERNET_VIRTIO_ETHERNET_H_
