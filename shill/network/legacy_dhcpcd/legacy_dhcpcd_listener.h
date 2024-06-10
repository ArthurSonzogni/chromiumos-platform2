// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_NETWORK_LEGACY_DHCPCD_LEGACY_DHCPCD_LISTENER_H_
#define SHILL_NETWORK_LEGACY_DHCPCD_LEGACY_DHCPCD_LISTENER_H_

#include <memory>

#include <base/functional/callback_forward.h>
#include <base/memory/ref_counted.h>
#include <dbus/bus.h>

#include "shill/event_dispatcher.h"
#include "shill/network/dhcp_client_proxy.h"
#include "shill/store/key_value_store.h"

namespace shill {

// The DHCPCD listener listens to signals from all DHCP clients and dispatches
// them through the LegacyDHCPCDProxyFactory.
class LegacyDHCPCDListener {
 public:
  // The status of the dhcpcd process.
  enum class Status {
    kInit,
    kBound,
    kRelease,
    kDiscover,
    kRequest,
    kRenew,
    kRebind,
    kArpSelf,
    kInform,
    kReboot,
    kNakDefer,
    kIPv6OnlyPreferred,
    kIgnoreInvalidOffer,
    kIgnoreFailedOffer,
    kIgnoreAdditionalOffer,
    kIgnoreNonOffer,
    kArpGateway,
  };

  using EventSignalCB =
      base::RepeatingCallback<void(std::string_view service_name,
                                   uint32_t pid,
                                   DHCPClientProxy::EventReason reason,
                                   const KeyValueStore& configuration)>;
  using StatusChangedCB = base::RepeatingCallback<void(
      std::string_view service_name, uint32_t pid, Status status)>;

  virtual ~LegacyDHCPCDListener() = default;
};

// The factory of LegacyDHCPCDListener. This interface is created for injecting
// a mock listener instance at testing.
class LegacyDHCPCDListenerFactory {
 public:
  using EventSignalCB = LegacyDHCPCDListener::EventSignalCB;
  using StatusChangedCB = LegacyDHCPCDListener::StatusChangedCB;

  LegacyDHCPCDListenerFactory() = default;
  virtual ~LegacyDHCPCDListenerFactory() = default;

  virtual std::unique_ptr<LegacyDHCPCDListener> Create(
      scoped_refptr<dbus::Bus> bus,
      EventDispatcher* dispatcher,
      EventSignalCB event_signal_cb,
      StatusChangedCB status_changed_cb);
};

}  // namespace shill
#endif  // SHILL_NETWORK_LEGACY_DHCPCD_LEGACY_DHCPCD_LISTENER_H_
