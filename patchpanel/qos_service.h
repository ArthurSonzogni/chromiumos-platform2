// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHPANEL_QOS_SERVICE_H_
#define PATCHPANEL_QOS_SERVICE_H_

namespace patchpanel {

class Datapath;

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
  ~QoSService();

  // QoSService is neither copyable nor movable.
  QoSService(const QoSService&) = delete;
  QoSService& operator=(const QoSService&) = delete;

  // TODO(b/296951862): Add implementation.

 private:
  Datapath* datapath_;
};

}  // namespace patchpanel

#endif  // PATCHPANEL_QOS_SERVICE_H_
