// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHPANEL_DBUS_CLIENT_NOTIFIER_H_
#define PATCHPANEL_DBUS_CLIENT_NOTIFIER_H_

#include <memory>

#include <net-base/ip_address.h>
#include <patchpanel/proto_bindings/patchpanel_service.pb.h>

#include "patchpanel/network_monitor_service.h"

namespace patchpanel {

// The notification callbacks to the client side.
class DbusClientNotifier {
 public:
  // Takes ownership of |virtual_device|.
  virtual void OnNetworkDeviceChanged(
      std::unique_ptr<NetworkDevice> virtual_device,
      NetworkDeviceChangedSignal::Event event) = 0;
  virtual void OnNetworkConfigurationChanged() = 0;
  virtual void OnNeighborReachabilityEvent(
      int ifindex,
      const net_base::IPAddress& ip_addr,
      NeighborLinkMonitor::NeighborRole role,
      NeighborReachabilityEventSignal::EventType event_type) = 0;
};

}  // namespace patchpanel
#endif  // PATCHPANEL_DBUS_CLIENT_NOTIFIER_H_
