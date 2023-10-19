// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHPANEL_QOS_SERVICE_H_
#define PATCHPANEL_QOS_SERVICE_H_

#include <memory>
#include <string>
#include <vector>

#include <base/containers/flat_set.h>
#include <net-base/dns_client.h>

#include "patchpanel/minijailed_process_runner.h"
#include "patchpanel/shill_client.h"

namespace patchpanel {

class Datapath;
class SocketConnectionEvent;

// QoSService manages the network QoS feature (Quality of Service), which:
// - Automatically classifies traffic into QoS categories;
// - Allows other components to explicitly associate traffic with certain QoS
//   categories;
// - Prioritizes traffic according to its QoS category (currently only for
//   egress traffic on WiFi interfaces by leveraging WiFi QoS/WMM).
//
// In general, this class mainly interacts with iptables and conntrack for QoS
// management:
// - On starting, QoSService will install a group of iptables rules for traffic
//   detection and DSCP marking. No jump rule will be added so these rules won't
//   be active on this stage.
// - Jump rules may be added or removed on 1) QoS feature is enabled or disabled
//   and 2) WiFi interface is added or removed.
// - Conntrack table is affected in two ways:
//   - There are iptables rules to save/restore the QoS category bits between
//     fwmark and connmark.
//   - Conntrack table will be updated directly from this class on QoS-related
//     socket connection events from other components.
//
class QoSService {
 public:
  explicit QoSService(Datapath* datapath);
  // Provided for testing.
  QoSService(Datapath* datapath,
             std::unique_ptr<net_base::DNSClientFactory> dns_client_factory,
             std::unique_ptr<MinijailedProcessRunner> process_runner);

  ~QoSService();

  // QoSService is neither copyable nor movable.
  QoSService(const QoSService&) = delete;
  QoSService& operator=(const QoSService&) = delete;

  // Enable or disable the QoS feature. Note that it will only affect new socket
  // connections. The QoS treatment for the existing connections may or may not
  // be changed.
  void Enable();
  void Disable();
  bool is_enabled() const { return is_enabled_; }

  // Listening to the shill Device change event for the per-interface setup.
  // Currently this class only care about WiFi interfaces.
  void OnPhysicalDeviceAdded(const ShillClient::Device& device);
  void OnPhysicalDeviceRemoved(const ShillClient::Device& device);

  // Process socket connection events from ARC App monitor and modify connmark
  // based on socket information.
  void ProcessSocketConnectionEvent(
      const patchpanel::SocketConnectionEvent& msg);

  // Update iptables rules (done by Datapath) for DoH.
  void UpdateDoHProviders(const ShillClient::DoHProviders& doh_providers);

 private:
  // Dependencies.
  Datapath* datapath_;
  std::unique_ptr<net_base::DNSClientFactory> dns_client_factory_;
  std::unique_ptr<MinijailedProcessRunner> process_runner_;

  // QoS feature is disabled by default. This value can be changed in `Enable()`
  // and `Disable()`.
  bool is_enabled_ = false;

  // Tracks the existing interfaces which this service cares about (currently
  // only WiFi interfaces). This class doesn't care about whether the interface
  // is connected (i.e., ready for routing) or not. We need to track this to
  // support the case that QoS feature is enabled after the WiFi interface
  // appeared.
  base::flat_set<std::string> interfaces_;

  // Defined in the qos_service.cc file. |doh_updator_| is responding for doing
  // the async DNS queries and call the corresponding function in Datapath to
  // update the iptables rules related to DoH. Reset in UpdateDoHProviders().
  class DoHUpdater;
  std::unique_ptr<DoHUpdater> doh_updater_;
};

}  // namespace patchpanel

#endif  // PATCHPANEL_QOS_SERVICE_H_
