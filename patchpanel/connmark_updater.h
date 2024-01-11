// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHPANEL_CONNMARK_UPDATER_H_
#define PATCHPANEL_CONNMARK_UPDATER_H_

#include <map>
#include <memory>
#include <string_view>
#include <utility>

#include "patchpanel/conntrack_monitor.h"
#include "patchpanel/minijailed_process_runner.h"
#include "patchpanel/routing_service.h"

namespace patchpanel {

// When trying to update connmark for UDP sockets, there is possibility that
// UDP sockets are not yet know in conntrack table at the moment and connmark
// update fails. More details can be found in: b/302076027.
// ConnmarkUpdater manages connmark updates for UDP socket connections,
// which:
// - Adds failed connmark update requests into a pending list.
// - Gets conntrack table updates from ConntrackMonitor and retries connmark
// update when pending connections appears in conntrack table.
//
// In general, this class mainly interacts with conntrack table and
// ConntrackMonitor for connmark update management:
// When a ConnmarkUpdater object is constructed:
//   - Register a listener on ConntrackMonitor when pending list is not empty,
//     and removes the listener when pending list becomes empty.
//   - When getting conntrack table updates from ConntrackMonitor, checks if the
//     entry is in the pending list. If so, update the connmark and removes the
//     entry from the pending list.
// Destructing this object will cancel all the pending requests and unregister
// listener on ConntrackMonitor.
// TODO(b/177389948): Add TCP handling in this class.
class ConnmarkUpdater {
 public:
  struct UDPConnection {
    net_base::IPAddress src_addr;
    net_base::IPAddress dst_addr;
    uint16_t sport;
    uint16_t dport;

    // 3-way comparison operator for able to be keyed in a map.
    friend std::strong_ordering operator<=>(const UDPConnection&,
                                            const UDPConnection&) = default;
  };

  explicit ConnmarkUpdater(ConntrackMonitor* monitor);
  // Provided for testing.
  ConnmarkUpdater(ConntrackMonitor* monitor,
                  std::unique_ptr<MinijailedProcessRunner> process_runner);

  virtual ~ConnmarkUpdater() = default;

  // ConnmarkUpdater is neither copyable nor movable.
  ConnmarkUpdater(const ConnmarkUpdater&) = delete;
  ConnmarkUpdater& operator=(const ConnmarkUpdater&) = delete;

  // Adds UDP socket connection for update.
  virtual void UpdateUDPConnectionConnmark(const UDPConnection& conn,
                                           Fwmark mark,
                                           Fwmark mask);

  // Gets size of the pending list, only used for testing.
  size_t GetPendingListSizeForTesting();

 private:
  // Handles conntrack events from ConntrackMonitor and updates connmark
  // for UDP connections in |pending_connections_| if applies.
  void HandleConntrackEvent(const ConntrackMonitor::Event& event);

  // Dependencies.
  std::unique_ptr<MinijailedProcessRunner> process_runner_;
  ConntrackMonitor* conntrack_monitor_;

  // Pending list of UDP connections whose connmark need to be updated.
  // Currently only UDP connections are added to this list since
  // the user of this manager (QoS service and traffic counter) ensures that
  // TCP connections are established on ARC side before SocketConnectionEvent
  // is sent. Entries in the list are removed as soon as the expected UDP
  // connection is observed through ConntrackMonitor, even if the connmark
  // update operation failed.
  std::map<UDPConnection, std::pair<Fwmark, Fwmark>> pending_connections_;

  // Listens to conntrack events.
  std::unique_ptr<ConntrackMonitor::Listener> listener_;

  base::WeakPtrFactory<ConnmarkUpdater> weak_factory_{this};
};

}  // namespace patchpanel

#endif  // PATCHPANEL_CONNMARK_UPDATER_H_
