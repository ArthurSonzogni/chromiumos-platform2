// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/counters_service.h"

#include <net/if.h>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <base/logging.h>
#include <base/test/task_environment.h>
#include <chromeos/dbus/service_constants.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "patchpanel/fake_process_runner.h"
#include "patchpanel/iptables.h"
#include "patchpanel/mock_connmark_updater.h"
#include "patchpanel/mock_conntrack_monitor.h"
#include "patchpanel/mock_datapath.h"
#include "patchpanel/noop_system.h"

namespace patchpanel {

using ::testing::_;
using ::testing::AnyNumber;
using ::testing::Contains;
using ::testing::Each;
using ::testing::ElementsAreArray;
using ::testing::Eq;
using ::testing::Lt;
using ::testing::Mock;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::SizeIs;
using ::testing::StrEq;

using Counter = CountersService::Counter;
using CounterKey = CountersService::CounterKey;

constexpr char kIPAddress1[] = "8.8.8.8";
constexpr char kIPAddress2[] = "8.8.8.4";
constexpr int kPort1 = 10000;
constexpr int kPort2 = 20000;

// The following four functions should be put outside the anonymous namespace
// otherwise they could not be found in the tests.
std::ostream& operator<<(std::ostream& os, const Counter& counter) {
  os << "rx_bytes:" << counter.rx_bytes << ", rx_packets:" << counter.rx_packets
     << ", tx_bytes:" << counter.tx_bytes
     << ", tx_packets:" << counter.tx_packets;
  return os;
}

std::ostream& operator<<(std::ostream& os, const CounterKey& key) {
  os << "ifname:" << key.ifname
     << ", source:" << TrafficCounter::Source_Name(key.source)
     << ", ip_family:" << TrafficCounter::IpFamily_Name(key.ip_family);
  return os;
}

namespace {
// The following string is copied from the real output of iptables v1.6.2 by
// `iptables -t mangle -L -x -v -n`. This output contains all the accounting
// chains/rules for eth0 and wlan0.
const char kIptablesOutput[] = R"(
Chain PREROUTING (policy ACCEPT 22785 packets, 136093545 bytes)
    pkts      bytes target     prot opt in     out     source               destination
      18     2196 MARK       all  --  arcbr0 *     0.0.0.0/0             0.0.0.0/0             MARK set 0x1
       0        0 MARK       all  --  vmtap+ *     0.0.0.0/0             0.0.0.0/0             MARK set 0x1
    6526 68051766 MARK       all  --  arc_eth0 *     0.0.0.0/0             0.0.0.0/0             MARK set 0x1
       9     1104 MARK       all  --  arc_wlan0 *     0.0.0.0/0             0.0.0.0/0             MARK set 0x1

Chain INPUT (policy ACCEPT 4421 packets, 2461233 bytes)
    pkts      bytes target     prot opt in     out     source               destination
  312491 1767147156 rx_eth0  all  --  eth0   *     0.0.0.0/0             0.0.0.0/0
       0        0 rx_wlan0  all  --  wlan0  *     0.0.0.0/0             0.0.0.0/0
       234 8776543 rx_mbimmux0.1  all  --  mbimmux0.1  *     0.0.0.0/0             0.0.0.0/0
    8870   805689 rx_mdns    udp  --  *      *     0.0.0.0/0            224.0.0.251          udp dpt:5353

Chain FORWARD (policy ACCEPT 18194 packets, 133612816 bytes)
    pkts      bytes target     prot opt in     out     source               destination
    6511 68041668 tx_eth0  all  --  *    eth0    0.0.0.0/0             0.0.0.0/0
   11683 65571148 rx_eth0  all  --  eth0   *     0.0.0.0/0             0.0.0.0/0
   1234 9876543 rx_mbimmux0.1  all  --  mbimmux0.1   *     0.0.0.0/0             0.0.0.0/0

Chain OUTPUT (policy ACCEPT 4574 packets, 2900995 bytes)
    pkts      bytes target     prot opt in     out     source               destination

Chain POSTROUTING (policy ACCEPT 22811 packets, 136518827 bytes)
    pkts      bytes target     prot opt in     out     source               destination
  202160 1807550291 tx_eth0  all  --  *    eth0    0.0.0.0/0             0.0.0.0/0             owner socket exists
       2       96 tx_wlan0  all  --  *    wlan0   0.0.0.0/0             0.0.0.0/0             owner socket exists

Chain rx_wifi_mdns (1 references)
    pkts      bytes target     prot opt in     out     source               destination         

Chain rx_ethernet_mdns (1 references)
    pkts      bytes target     prot opt in     out     source               destination         

Chain rx_mdns (1 references)
    pkts      bytes target     prot opt in     out     source               destination         
    8867   805299 rx_ethernet_mdns  all  --  eth0   *       0.0.0.0/0            0.0.0.0/0           
       0        0 rx_wifi_mdns  all  --  wlan0  *       0.0.0.0/0            0.0.0.0/0    

Chain tx_eth0 (1 references)
    pkts      bytes target     prot opt in     out     source               destination
    1366   244427 RETURN     all  --  *    *     0.0.0.0/0             0.0.0.0/0             mark match 0x100/0x3f00
       0        0 RETURN     all  --  *    *     0.0.0.0/0             0.0.0.0/0             mark match 0x200/0x3f00
      20     1670 RETURN     all  --  *    *     0.0.0.0/0             0.0.0.0/0             mark match 0x300/0x3f00
     550   138402 RETURN     all  --  *    *     0.0.0.0/0             0.0.0.0/0             mark match 0x400/0x3f00
       0        0 RETURN     all  --  *    *     0.0.0.0/0             0.0.0.0/0             mark match 0x500/0x3f00
    5374   876172 RETURN     all  --  *    *     0.0.0.0/0             0.0.0.0/0             mark match 0x2000/0x3f00
      39     2690 RETURN     all  --  *    *     0.0.0.0/0             0.0.0.0/0             mark match 0x2100/0x3f00
       0        0 RETURN     all  --  *    *     0.0.0.0/0             0.0.0.0/0             mark match 0x2200/0x3f00
       0        0 RETURN     all  --  *    *     0.0.0.0/0             0.0.0.0/0             mark match 0x2300/0x3f00
       0        0 RETURN     all  --  *    *     0.0.0.0/0             0.0.0.0/0             mark match 0x2400/0x3f00
       4      123            all  --  *    *     0.0.0.0/0             0.0.0.0/0

Chain tx_wlan0 (1 references)
    pkts      bytes target     prot opt in     out     source               destination
     310    57004 RETURN     all  --  *    *     0.0.0.0/0             0.0.0.0/0             mark match 0x100/0x3f00
       0        0 RETURN     all  --  *    *     0.0.0.0/0             0.0.0.0/0             mark match 0x200/0x3f00
       0        0 RETURN     all  --  *    *     0.0.0.0/0             0.0.0.0/0             mark match 0x300/0x3f00
      24     2801 RETURN     all  --  *    *     0.0.0.0/0             0.0.0.0/0             mark match 0x400/0x3f00
       0        0 RETURN     all  --  *    *     0.0.0.0/0             0.0.0.0/0             mark match 0x500/0x3f00
       0        0 RETURN     all  --  *    *     0.0.0.0/0             0.0.0.0/0             mark match 0x2000/0x3f00
       0        0 RETURN     all  --  *    *     0.0.0.0/0             0.0.0.0/0             mark match 0x2100/0x3f00
       0        0 RETURN     all  --  *    *     0.0.0.0/0             0.0.0.0/0             mark match 0x2200/0x3f00
       0        0 RETURN     all  --  *    *     0.0.0.0/0             0.0.0.0/0             mark match 0x2300/0x3f00
       0        0 RETURN     all  --  *    *     0.0.0.0/0             0.0.0.0/0             mark match 0x2400/0x3f00
       0        0            all  --  *    *     0.0.0.0/0             0.0.0.0/0

Chain tx_mbimmux0.1 (1 references)
    pkts      bytes target     prot opt in     out     source               destination
    3221   997243 RETURN     all  --  *      *       0.0.0.0/0            0.0.0.0/0            mark match 0x100/0x3f00
     116    12471 RETURN     all  --  *      *       0.0.0.0/0            0.0.0.0/0            mark match 0x200/0x3f00
       0        0 RETURN     all  --  *      *       0.0.0.0/0            0.0.0.0/0            mark match 0x300/0x3f00
     239    30507 RETURN     all  --  *      *       0.0.0.0/0            0.0.0.0/0            mark match 0x400/0x3f00
       0        0 RETURN     all  --  *      *       0.0.0.0/0            0.0.0.0/0            mark match 0x500/0x3f00
     138    16239 RETURN     all  --  *      *       0.0.0.0/0            0.0.0.0/0            mark match 0x2000/0x3f00
       0        0 RETURN     all  --  *      *       0.0.0.0/0            0.0.0.0/0            mark match 0x2600/0x3f00
       0        0 RETURN     all  --  *      *       0.0.0.0/0            0.0.0.0/0            mark match 0x2500/0x3f00
       0        0 RETURN     all  --  *      *       0.0.0.0/0            0.0.0.0/0            mark match 0x2100/0x3f00
       0        0 RETURN     all  --  *      *       0.0.0.0/0            0.0.0.0/0            mark match 0x2200/0x3f00
       0        0 RETURN     all  --  *      *       0.0.0.0/0            0.0.0.0/0            mark match 0x2300/0x3f00
       0        0 RETURN     all  --  *      *       0.0.0.0/0            0.0.0.0/0            mark match 0x2700/0x3f00
       0        0 RETURN     all  --  *      *       0.0.0.0/0            0.0.0.0/0            mark match 0x2800/0x3f00
       0        0 RETURN     all  --  *      *       0.0.0.0/0            0.0.0.0/0            mark match 0x2400/0x3f00
       0        0            all  --  *      *       0.0.0.0/0            0.0.0.0/0

Chain rx_eth0 (2 references)
 pkts bytes target     prot opt in     out     source               destination
   73 11938 RETURN     all  --  *    *     0.0.0.0/0             0.0.0.0/0             mark match 0x100/0x3f00
    0     0 RETURN     all  --  *    *     0.0.0.0/0             0.0.0.0/0             mark match 0x200/0x3f00
    0     0 RETURN     all  --  *    *     0.0.0.0/0             0.0.0.0/0             mark match 0x300/0x3f00
    5   694 RETURN     all  --  *    *     0.0.0.0/0             0.0.0.0/0             mark match 0x400/0x3f00
    0     0 RETURN     all  --  *    *     0.0.0.0/0             0.0.0.0/0             mark match 0x500/0x3f00
    0     0 RETURN     all  --  *    *     0.0.0.0/0             0.0.0.0/0             mark match 0x2000/0x3f00
    0     0 RETURN     all  --  *    *     0.0.0.0/0             0.0.0.0/0             mark match 0x2100/0x3f00
    0     0 RETURN     all  --  *    *     0.0.0.0/0             0.0.0.0/0             mark match 0x2200/0x3f00
    0     0 RETURN     all  --  *    *     0.0.0.0/0             0.0.0.0/0             mark match 0x2300/0x3f00
    0     0 RETURN     all  --  *    *     0.0.0.0/0             0.0.0.0/0             mark match 0x2400/0x3f00
    6   345            all  --  *    *     0.0.0.0/0             0.0.0.0/0

Chain rx_wlan0 (2 references)
    pkts      bytes target     prot opt in     out     source               destination
     153    28098 RETURN     all  --  *    *     0.0.0.0/0             0.0.0.0/0             mark match 0x100/0x3f00
       0        0 RETURN     all  --  *    *     0.0.0.0/0             0.0.0.0/0             mark match 0x200/0x3f00
       0        0 RETURN     all  --  *    *     0.0.0.0/0             0.0.0.0/0             mark match 0x300/0x3f00
       6      840 RETURN     all  --  *    *     0.0.0.0/0             0.0.0.0/0             mark match 0x400/0x3f00
       0        0 RETURN     all  --  *    *     0.0.0.0/0             0.0.0.0/0             mark match 0x500/0x3f00
       0        0 RETURN     all  --  *    *     0.0.0.0/0             0.0.0.0/0             mark match 0x2000/0x3f00
       0        0 RETURN     all  --  *    *     0.0.0.0/0             0.0.0.0/0             mark match 0x2100/0x3f00
       0        0 RETURN     all  --  *    *     0.0.0.0/0             0.0.0.0/0             mark match 0x2200/0x3f00
       0        0 RETURN     all  --  *    *     0.0.0.0/0             0.0.0.0/0             mark match 0x2300/0x3f00
       0        0 RETURN     all  --  *    *     0.0.0.0/0             0.0.0.0/0             mark match 0x2400/0x3f00
       0        0            all  --  *    *     0.0.0.0/0             0.0.0.0/0

Chain rx_mbimmux0.1 (2 references)
    pkts      bytes target     prot opt in     out     source               destination
    3607  1847697 RETURN     all  --  *      *       0.0.0.0/0            0.0.0.0/0            mark match 0x100/0x3f00
     180    31066 RETURN     all  --  *      *       0.0.0.0/0            0.0.0.0/0            mark match 0x200/0x3f00
       0        0 RETURN     all  --  *      *       0.0.0.0/0            0.0.0.0/0            mark match 0x300/0x3f00
      69    25577 RETURN     all  --  *      *       0.0.0.0/0            0.0.0.0/0            mark match 0x400/0x3f00
       0        0 RETURN     all  --  *      *       0.0.0.0/0            0.0.0.0/0            mark match 0x500/0x3f00
     152    61218 RETURN     all  --  *      *       0.0.0.0/0            0.0.0.0/0            mark match 0x2000/0x3f00
       0        0 RETURN     all  --  *      *       0.0.0.0/0            0.0.0.0/0            mark match 0x2600/0x3f00
       0        0 RETURN     all  --  *      *       0.0.0.0/0            0.0.0.0/0            mark match 0x2500/0x3f00
       0        0 RETURN     all  --  *      *       0.0.0.0/0            0.0.0.0/0            mark match 0x2100/0x3f00
       0        0 RETURN     all  --  *      *       0.0.0.0/0            0.0.0.0/0            mark match 0x2200/0x3f00
       0        0 RETURN     all  --  *      *       0.0.0.0/0            0.0.0.0/0            mark match 0x2300/0x3f00
       0        0 RETURN     all  --  *      *       0.0.0.0/0            0.0.0.0/0            mark match 0x2700/0x3f00
       0        0 RETURN     all  --  *      *       0.0.0.0/0            0.0.0.0/0            mark match 0x2800/0x3f00
       0        0 RETURN     all  --  *      *       0.0.0.0/0            0.0.0.0/0            mark match 0x2400/0x3f00
       3      120            all  --  *      *       0.0.0.0/0            0.0.0.0/0
)";

const char kIp6tablesOutput[] = R"(
Chain PREROUTING (policy ACCEPT 22785 packets, 136093545 bytes)
    pkts      bytes target     prot opt in     out     source               destination
      18     2196 MARK       all  --  arcbr0 *     ::/0             ::/0             MARK set 0x1
       0        0 MARK       all  --  vmtap+ *     ::/0             ::/0             MARK set 0x1
    6526 68051766 MARK       all  --  arc_eth0 *     ::/0             ::/0             MARK set 0x1
       9     1104 MARK       all  --  arc_wlan0 *     ::/0             ::/0             MARK set 0x1

Chain INPUT (policy ACCEPT 4421 packets, 2461233 bytes)
    pkts      bytes target     prot opt in     out     source               destination
  312491 1767147156 rx_eth0  all  --  eth0   *     ::/0             ::/0
       0        0 rx_wlan0  all  --  wlan0  *     ::/0             ::/0

Chain FORWARD (policy ACCEPT 18194 packets, 133612816 bytes)
    pkts      bytes target     prot opt in     out     source               destination
    6511 68041668 tx_eth0  all  --  *    eth0    ::/0             ::/0
   11683 65571148 rx_eth0  all  --  eth0   *     ::/0             ::/0

Chain OUTPUT (policy ACCEPT 4574 packets, 2900995 bytes)
    pkts      bytes target     prot opt in     out     source               destination

Chain POSTROUTING (policy ACCEPT 22811 packets, 136518827 bytes)
    pkts      bytes target     prot opt in     out     source               destination
  202160 1807550291 tx_eth0  all  --  *    eth0    ::/0             ::/0             owner socket exists
       2       96 tx_wlan0  all  --  *    wlan0   ::/0             ::/0             owner socket exists

Chain tx_eth0 (1 references)
    pkts      bytes target     prot opt in     out     source               destination
    1366   244427 RETURN     all  --  *    *     ::/0             ::/0             mark match 0x100/0x3f00
       0        0 RETURN     all  --  *    *     ::/0             ::/0             mark match 0x200/0x3f00
      20     1670 RETURN     all  --  *    *     ::/0             ::/0             mark match 0x300/0x3f00
     550   138402 RETURN     all  --  *    *     ::/0             ::/0             mark match 0x400/0x3f00
       0        0 RETURN     all  --  *    *     ::/0             ::/0             mark match 0x500/0x3f00
    5374   876172 RETURN     all  --  *    *     ::/0             ::/0             mark match 0x2000/0x3f00
      39     2690 RETURN     all  --  *    *     ::/0             ::/0             mark match 0x2100/0x3f00
       0        0 RETURN     all  --  *    *     ::/0             ::/0             mark match 0x2200/0x3f00
       0        0 RETURN     all  --  *    *     ::/0             ::/0             mark match 0x2300/0x3f00
       0        0 RETURN     all  --  *    *     ::/0             ::/0             mark match 0x2400/0x3f00
       4      123            all  --  *    *     ::/0             ::/0

Chain tx_wlan0 (1 references)
    pkts      bytes target     prot opt in     out     source               destination
     310    57004 RETURN     all  --  *    *     ::/0             ::/0             mark match 0x100/0x3f00
       0        0 RETURN     all  --  *    *     ::/0             ::/0             mark match 0x200/0x3f00
       0        0 RETURN     all  --  *    *     ::/0             ::/0             mark match 0x300/0x3f00
      24     2801 RETURN     all  --  *    *     ::/0             ::/0             mark match 0x400/0x3f00
       0        0 RETURN     all  --  *    *     ::/0             ::/0             mark match 0x500/0x3f00
       0        0 RETURN     all  --  *    *     ::/0             ::/0             mark match 0x2000/0x3f00
       0        0 RETURN     all  --  *    *     ::/0             ::/0             mark match 0x2100/0x3f00
       0        0 RETURN     all  --  *    *     ::/0             ::/0             mark match 0x2200/0x3f00
       0        0 RETURN     all  --  *    *     ::/0             ::/0             mark match 0x2300/0x3f00
       0        0 RETURN     all  --  *    *     ::/0             ::/0             mark match 0x2400/0x3f00
       0        0            all  --  *    *     ::/0             ::/0

Chain tx_mbimmux0.1 (1 references)
    pkts      bytes target     prot opt in     out     source               destination
    3862  1178768 RETURN     all      *      *       ::/0                 ::/0                 mark match 0x100/0x3f00
       0        0 RETURN     all      *      *       ::/0                 ::/0                 mark match 0x200/0x3f00
       0        0 RETURN     all      *      *       ::/0                 ::/0                 mark match 0x300/0x3f00
      37    12855 RETURN     all      *      *       ::/0                 ::/0                 mark match 0x400/0x3f00
       0        0 RETURN     all      *      *       ::/0                 ::/0                 mark match 0x500/0x3f00
      69    11435 RETURN     all      *      *       ::/0                 ::/0                 mark match 0x2000/0x3f00
       0        0 RETURN     all      *      *       ::/0                 ::/0                 mark match 0x2600/0x3f00
       0        0 RETURN     all      *      *       ::/0                 ::/0                 mark match 0x2500/0x3f00
       0        0 RETURN     all      *      *       ::/0                 ::/0                 mark match 0x2100/0x3f00
       0        0 RETURN     all      *      *       ::/0                 ::/0                 mark match 0x2200/0x3f00
       0        0 RETURN     all      *      *       ::/0                 ::/0                 mark match 0x2300/0x3f00
       0        0 RETURN     all      *      *       ::/0                 ::/0                 mark match 0x2700/0x3f00
       0        0 RETURN     all      *      *       ::/0                 ::/0                 mark match 0x2800/0x3f00
       0        0 RETURN     all      *      *       ::/0                 ::/0                 mark match 0x2400/0x3f00
       0        0            all      *      *       ::/0                 ::/0

Chain rx_eth0 (2 references)
 pkts bytes target     prot opt in     out     source               destination
   73 11938 RETURN     all  --  *    *     ::/0             ::/0             mark match 0x100/0x3f00
    0     0 RETURN     all  --  *    *     ::/0             ::/0             mark match 0x200/0x3f00
    0     0 RETURN     all  --  *    *     ::/0             ::/0             mark match 0x300/0x3f00
    5   694 RETURN     all  --  *    *     ::/0             ::/0             mark match 0x400/0x3f00
    0     0 RETURN     all  --  *    *     ::/0             ::/0             mark match 0x500/0x3f00
    0     0 RETURN     all  --  *    *     ::/0             ::/0             mark match 0x2000/0x3f00
    0     0 RETURN     all  --  *    *     ::/0             ::/0             mark match 0x2100/0x3f00
    0     0 RETURN     all  --  *    *     ::/0             ::/0             mark match 0x2200/0x3f00
    0     0 RETURN     all  --  *    *     ::/0             ::/0             mark match 0x2300/0x3f00
    0     0 RETURN     all  --  *    *     ::/0             ::/0             mark match 0x2400/0x3f00
    6   345            all  --  *    *     ::/0             ::/0

Chain rx_wlan0 (2 references)
    pkts      bytes target     prot opt in     out     source               destination
     153    28098 RETURN     all  --  *    *     ::/0             ::/0             mark match 0x100/0x3f00
       0        0 RETURN     all  --  *    *     ::/0             ::/0             mark match 0x200/0x3f00
       0        0 RETURN     all  --  *    *     ::/0             ::/0             mark match 0x300/0x3f00
       6      840 RETURN     all  --  *    *     ::/0             ::/0             mark match 0x400/0x3f00
       0        0 RETURN     all  --  *    *     ::/0             ::/0             mark match 0x500/0x3f00
       0        0 RETURN     all  --  *    *     ::/0             ::/0             mark match 0x2000/0x3f00
       0        0 RETURN     all  --  *    *     ::/0             ::/0             mark match 0x2100/0x3f00
       0        0 RETURN     all  --  *    *     ::/0             ::/0             mark match 0x2200/0x3f00
       0        0 RETURN     all  --  *    *     ::/0             ::/0             mark match 0x2300/0x3f00
       0        0 RETURN     all  --  *    *     ::/0             ::/0             mark match 0x2400/0x3f00
       0        0            all  --  *    *     ::/0             ::/0

Chain rx_mbimmux0.1 (2 references)
    pkts      bytes target     prot opt in     out     source               destination
    9247  9763672 RETURN     all      *      *       ::/0                 ::/0                 mark match 0x100/0x3f00
       0        0 RETURN     all      *      *       ::/0                 ::/0                 mark match 0x200/0x3f00
       0        0 RETURN     all      *      *       ::/0                 ::/0                 mark match 0x300/0x3f00
       1       72 RETURN     all      *      *       ::/0                 ::/0                 mark match 0x400/0x3f00
       0        0 RETURN     all      *      *       ::/0                 ::/0                 mark match 0x500/0x3f00
      70    29640 RETURN     all      *      *       ::/0                 ::/0                 mark match 0x2000/0x3f00
       0        0 RETURN     all      *      *       ::/0                 ::/0                 mark match 0x2600/0x3f00
       0        0 RETURN     all      *      *       ::/0                 ::/0                 mark match 0x2500/0x3f00
       0        0 RETURN     all      *      *       ::/0                 ::/0                 mark match 0x2100/0x3f00
       0        0 RETURN     all      *      *       ::/0                 ::/0                 mark match 0x2200/0x3f00
       0        0 RETURN     all      *      *       ::/0                 ::/0                 mark match 0x2300/0x3f00
       0        0 RETURN     all      *      *       ::/0                 ::/0                 mark match 0x2700/0x3f00
       0        0 RETURN     all      *      *       ::/0                 ::/0                 mark match 0x2800/0x3f00
       0        0 RETURN     all      *      *       ::/0                 ::/0                 mark match 0x2400/0x3f00
      10      960            all      *      *       ::/0                 ::/0
)";

bool CompareCounters(std::map<CounterKey, Counter> expected,
                     std::map<CounterKey, Counter> actual) {
  bool success = true;
  for (const auto& kv : expected) {
    const auto it = actual.find(kv.first);
    if (it == actual.end()) {
      LOG(ERROR) << "Could not find expected CounterKey=" << kv.first;
      success = false;
      continue;
    }
    if (!(it->second == kv.second)) {
      LOG(ERROR) << "Unexpected Counter=" << it->second
                 << " for CounterKey=" << kv.first << ". Expected instead "
                 << kv.second;
      success = false;
    }
  }
  for (const auto& kv : actual) {
    if (expected.find(kv.first) == expected.end()) {
      LOG(ERROR) << "Unexpected entry CounterKey=" << kv.first
                 << " Counter=" << kv.second;
      success = false;
    }
  }
  return success;
}

class CountersServiceTest : public testing::Test {
 protected:
  CountersServiceTest()
      : datapath_(&process_runner_, &system_),
        counters_svc_(&datapath_, &conntrack_monitor_) {}

  // Makes `iptables` and `ip6tables` returning |ipv4_output| and
  // |ipv6_output|, respectively. Expects an empty map from GetCounters().
  void TestBadIptablesOutput(const std::string& ipv4_output,
                             const std::string& ipv6_output) {
    EXPECT_CALL(datapath_,
                DumpIptables(IpFamily::kIPv4, Iptables::Table::kMangle))
        .WillRepeatedly(Return(ipv4_output));
    EXPECT_CALL(datapath_,
                DumpIptables(IpFamily::kIPv6, Iptables::Table::kMangle))
        .WillRepeatedly(Return(ipv6_output));

    auto actual = counters_svc_.GetCounters({});
    std::map<CounterKey, Counter> expected;
    EXPECT_TRUE(CompareCounters(expected, actual));
  }

  // Note that this needs to be initialized at first, since the ctors of other
  // members may rely on it (e.g., FileDescriptorWatcher).
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::MainThreadType::IO};

  FakeProcessRunner process_runner_;
  NoopSystem system_;
  NiceMock<MockDatapath> datapath_;
  MockConntrackMonitor conntrack_monitor_;
  CountersService counters_svc_;
};

TEST_F(CountersServiceTest, OnPhysicalDeviceAdded) {
  // The following commands are expected when eth0 comes up.
  EXPECT_CALL(datapath_,
              CheckChain(IpFamily::kDual, Iptables::Table::kMangle, "rx_eth0"))
      .WillOnce(Return(false));
  EXPECT_CALL(datapath_,
              CheckChain(IpFamily::kDual, Iptables::Table::kMangle, "tx_eth0"))
      .WillOnce(Return(false));
  EXPECT_CALL(datapath_,
              AddChain(IpFamily::kDual, Iptables::Table::kMangle, "rx_eth0"))
      .WillOnce(Return(true));
  EXPECT_CALL(datapath_,
              AddChain(IpFamily::kDual, Iptables::Table::kMangle, "tx_eth0"))
      .WillOnce(Return(true));
  const struct {
    Iptables::Command command;
    std::string_view chain;
    std::vector<std::string> argv;
  } expected_calls[] = {
      {Iptables::Command::kA, "INPUT", {"-i", "eth0", "-j", "rx_eth0", "-w"}},
      {Iptables::Command::kA, "FORWARD", {"-i", "eth0", "-j", "rx_eth0", "-w"}},
      {Iptables::Command::kA,
       "POSTROUTING",
       {"-o", "eth0", "-j", "tx_eth0", "-w"}},
      {Iptables::Command::kA,
       "tx_eth0",
       {"-m", "mark", "--mark", "0x00000100/0x00003f00", "-j", "RETURN", "-w"}},
      {Iptables::Command::kA,
       "tx_eth0",
       {"-m", "mark", "--mark", "0x00000200/0x00003f00", "-j", "RETURN", "-w"}},
      {Iptables::Command::kA,
       "tx_eth0",
       {"-m", "mark", "--mark", "0x00000300/0x00003f00", "-j", "RETURN", "-w"}},
      {Iptables::Command::kA,
       "tx_eth0",
       {"-m", "mark", "--mark", "0x00000400/0x00003f00", "-j", "RETURN", "-w"}},
      {Iptables::Command::kA,
       "tx_eth0",
       {"-m", "mark", "--mark", "0x00000500/0x00003f00", "-j", "RETURN", "-w"}},
      {Iptables::Command::kA,
       "tx_eth0",
       {"-m", "mark", "--mark", "0x00002000/0x00003f00", "-j", "RETURN", "-w"}},
      {Iptables::Command::kA,
       "tx_eth0",
       {"-m", "mark", "--mark", "0x00002100/0x00003f00", "-j", "RETURN", "-w"}},
      {Iptables::Command::kA,
       "tx_eth0",
       {"-m", "mark", "--mark", "0x00002200/0x00003f00", "-j", "RETURN", "-w"}},
      {Iptables::Command::kA,
       "tx_eth0",
       {"-m", "mark", "--mark", "0x00002300/0x00003f00", "-j", "RETURN", "-w"}},
      {Iptables::Command::kA,
       "tx_eth0",
       {"-m", "mark", "--mark", "0x00002400/0x00003f00", "-j", "RETURN", "-w"}},
      {Iptables::Command::kA,
       "tx_eth0",
       {"-m", "mark", "--mark", "0x00002500/0x00003f00", "-j", "RETURN", "-w"}},
      {Iptables::Command::kA,
       "tx_eth0",
       {"-m", "mark", "--mark", "0x00002600/0x00003f00", "-j", "RETURN", "-w"}},
      {Iptables::Command::kA,
       "tx_eth0",
       {"-m", "mark", "--mark", "0x00002700/0x00003f00", "-j", "RETURN", "-w"}},
      {Iptables::Command::kA,
       "tx_eth0",
       {"-m", "mark", "--mark", "0x00002800/0x00003f00", "-j", "RETURN", "-w"}},
      {Iptables::Command::kA,
       "rx_eth0",
       {"-m", "mark", "--mark", "0x00000100/0x00003f00", "-j", "RETURN", "-w"}},
      {Iptables::Command::kA,
       "rx_eth0",
       {"-m", "mark", "--mark", "0x00000200/0x00003f00", "-j", "RETURN", "-w"}},
      {Iptables::Command::kA,
       "rx_eth0",
       {"-m", "mark", "--mark", "0x00000300/0x00003f00", "-j", "RETURN", "-w"}},
      {Iptables::Command::kA,
       "rx_eth0",
       {"-m", "mark", "--mark", "0x00000400/0x00003f00", "-j", "RETURN", "-w"}},
      {Iptables::Command::kA,
       "rx_eth0",
       {"-m", "mark", "--mark", "0x00000500/0x00003f00", "-j", "RETURN", "-w"}},
      {Iptables::Command::kA,
       "rx_eth0",
       {"-m", "mark", "--mark", "0x00002000/0x00003f00", "-j", "RETURN", "-w"}},
      {Iptables::Command::kA,
       "rx_eth0",
       {"-m", "mark", "--mark", "0x00002100/0x00003f00", "-j", "RETURN", "-w"}},
      {Iptables::Command::kA,
       "rx_eth0",
       {"-m", "mark", "--mark", "0x00002200/0x00003f00", "-j", "RETURN", "-w"}},
      {Iptables::Command::kA,
       "rx_eth0",
       {"-m", "mark", "--mark", "0x00002300/0x00003f00", "-j", "RETURN", "-w"}},
      {Iptables::Command::kA,
       "rx_eth0",
       {"-m", "mark", "--mark", "0x00002400/0x00003f00", "-j", "RETURN", "-w"}},
      {Iptables::Command::kA,
       "rx_eth0",
       {"-m", "mark", "--mark", "0x00002500/0x00003f00", "-j", "RETURN", "-w"}},
      {Iptables::Command::kA,
       "rx_eth0",
       {"-m", "mark", "--mark", "0x00002600/0x00003f00", "-j", "RETURN", "-w"}},
      {Iptables::Command::kA,
       "rx_eth0",
       {"-m", "mark", "--mark", "0x00002700/0x00003f00", "-j", "RETURN", "-w"}},
      {Iptables::Command::kA,
       "rx_eth0",
       {"-m", "mark", "--mark", "0x00002800/0x00003f00", "-j", "RETURN", "-w"}},
      {Iptables::Command::kA, "tx_eth0", {"-w"}},
      {Iptables::Command::kA, "rx_eth0", {"-w"}},
  };

  for (const auto& rule : expected_calls) {
    EXPECT_CALL(
        datapath_,
        ModifyIptables(IpFamily::kDual, Iptables::Table::kMangle, rule.command,
                       StrEq(rule.chain), ElementsAreArray(rule.argv), _));
  }

  counters_svc_.OnPhysicalDeviceAdded("eth0");
}

TEST_F(CountersServiceTest, OnPhysicalDeviceRemoved) {
  const struct {
    Iptables::Command command;
    std::string_view chain;
    std::vector<std::string> argv;
  } expected_calls[] = {
      {Iptables::Command::kD, "INPUT", {"-i", "eth0", "-j", "rx_eth0", "-w"}},
      {Iptables::Command::kD, "FORWARD", {"-i", "eth0", "-j", "rx_eth0", "-w"}},
      {Iptables::Command::kD,
       "POSTROUTING",
       {"-o", "eth0", "-j", "tx_eth0", "-w"}},
  };

  for (const auto& rule : expected_calls) {
    EXPECT_CALL(
        datapath_,
        ModifyIptables(IpFamily::kDual, Iptables::Table::kMangle, rule.command,
                       StrEq(rule.chain), ElementsAreArray(rule.argv), _));
  }

  counters_svc_.OnPhysicalDeviceRemoved("eth0");
}

TEST_F(CountersServiceTest, OnMultiplexedCellularDeviceAdded) {
  // The following commands are expected when mbimmux0.1 comes up.
  EXPECT_CALL(datapath_, CheckChain(IpFamily::kDual, Iptables::Table::kMangle,
                                    "rx_mbimmux0.1"))
      .WillOnce(Return(false));
  EXPECT_CALL(datapath_, CheckChain(IpFamily::kDual, Iptables::Table::kMangle,
                                    "tx_mbimmux0.1"))
      .WillOnce(Return(false));
  EXPECT_CALL(datapath_, AddChain(IpFamily::kDual, Iptables::Table::kMangle,
                                  "rx_mbimmux0.1"))
      .WillOnce(Return(true));
  EXPECT_CALL(datapath_, AddChain(IpFamily::kDual, Iptables::Table::kMangle,
                                  "tx_mbimmux0.1"))
      .WillOnce(Return(true));
  const struct {
    Iptables::Command command;
    std::string_view chain;
    std::vector<std::string> argv;
  } expected_calls[] = {
      {Iptables::Command::kA,
       "INPUT",
       {"-i", "mbimmux0.1", "-j", "rx_mbimmux0.1", "-w"}},
      {Iptables::Command::kA,
       "FORWARD",
       {"-i", "mbimmux0.1", "-j", "rx_mbimmux0.1", "-w"}},
      {Iptables::Command::kA,
       "POSTROUTING",
       {"-o", "mbimmux0.1", "-j", "tx_mbimmux0.1", "-w"}},
      {Iptables::Command::kA,
       "tx_mbimmux0.1",
       {"-m", "mark", "--mark", "0x00000100/0x00003f00", "-j", "RETURN", "-w"}},
      {Iptables::Command::kA,
       "tx_mbimmux0.1",
       {"-m", "mark", "--mark", "0x00000200/0x00003f00", "-j", "RETURN", "-w"}},
      {Iptables::Command::kA,
       "tx_mbimmux0.1",
       {"-m", "mark", "--mark", "0x00000300/0x00003f00", "-j", "RETURN", "-w"}},
      {Iptables::Command::kA,
       "tx_mbimmux0.1",
       {"-m", "mark", "--mark", "0x00000400/0x00003f00", "-j", "RETURN", "-w"}},
      {Iptables::Command::kA,
       "tx_mbimmux0.1",
       {"-m", "mark", "--mark", "0x00000500/0x00003f00", "-j", "RETURN", "-w"}},
      {Iptables::Command::kA,
       "tx_mbimmux0.1",
       {"-m", "mark", "--mark", "0x00002000/0x00003f00", "-j", "RETURN", "-w"}},
      {Iptables::Command::kA,
       "tx_mbimmux0.1",
       {"-m", "mark", "--mark", "0x00002100/0x00003f00", "-j", "RETURN", "-w"}},
      {Iptables::Command::kA,
       "tx_mbimmux0.1",
       {"-m", "mark", "--mark", "0x00002200/0x00003f00", "-j", "RETURN", "-w"}},
      {Iptables::Command::kA,
       "tx_mbimmux0.1",
       {"-m", "mark", "--mark", "0x00002300/0x00003f00", "-j", "RETURN", "-w"}},
      {Iptables::Command::kA,
       "tx_mbimmux0.1",
       {"-m", "mark", "--mark", "0x00002400/0x00003f00", "-j", "RETURN", "-w"}},
      {Iptables::Command::kA,
       "tx_mbimmux0.1",
       {"-m", "mark", "--mark", "0x00002500/0x00003f00", "-j", "RETURN", "-w"}},
      {Iptables::Command::kA,
       "tx_mbimmux0.1",
       {"-m", "mark", "--mark", "0x00002600/0x00003f00", "-j", "RETURN", "-w"}},
      {Iptables::Command::kA,
       "tx_mbimmux0.1",
       {"-m", "mark", "--mark", "0x00002700/0x00003f00", "-j", "RETURN", "-w"}},
      {Iptables::Command::kA,
       "tx_mbimmux0.1",
       {"-m", "mark", "--mark", "0x00002800/0x00003f00", "-j", "RETURN", "-w"}},
      {Iptables::Command::kA,
       "rx_mbimmux0.1",
       {"-m", "mark", "--mark", "0x00000100/0x00003f00", "-j", "RETURN", "-w"}},
      {Iptables::Command::kA,
       "rx_mbimmux0.1",
       {"-m", "mark", "--mark", "0x00000200/0x00003f00", "-j", "RETURN", "-w"}},
      {Iptables::Command::kA,
       "rx_mbimmux0.1",
       {"-m", "mark", "--mark", "0x00000300/0x00003f00", "-j", "RETURN", "-w"}},
      {Iptables::Command::kA,
       "rx_mbimmux0.1",
       {"-m", "mark", "--mark", "0x00000400/0x00003f00", "-j", "RETURN", "-w"}},
      {Iptables::Command::kA,
       "rx_mbimmux0.1",
       {"-m", "mark", "--mark", "0x00000500/0x00003f00", "-j", "RETURN", "-w"}},
      {Iptables::Command::kA,
       "rx_mbimmux0.1",
       {"-m", "mark", "--mark", "0x00002000/0x00003f00", "-j", "RETURN", "-w"}},
      {Iptables::Command::kA,
       "rx_mbimmux0.1",
       {"-m", "mark", "--mark", "0x00002100/0x00003f00", "-j", "RETURN", "-w"}},
      {Iptables::Command::kA,
       "rx_mbimmux0.1",
       {"-m", "mark", "--mark", "0x00002200/0x00003f00", "-j", "RETURN", "-w"}},
      {Iptables::Command::kA,
       "rx_mbimmux0.1",
       {"-m", "mark", "--mark", "0x00002300/0x00003f00", "-j", "RETURN", "-w"}},
      {Iptables::Command::kA,
       "rx_mbimmux0.1",
       {"-m", "mark", "--mark", "0x00002400/0x00003f00", "-j", "RETURN", "-w"}},
      {Iptables::Command::kA,
       "rx_mbimmux0.1",
       {"-m", "mark", "--mark", "0x00002500/0x00003f00", "-j", "RETURN", "-w"}},
      {Iptables::Command::kA,
       "rx_mbimmux0.1",
       {"-m", "mark", "--mark", "0x00002600/0x00003f00", "-j", "RETURN", "-w"}},
      {Iptables::Command::kA,
       "rx_mbimmux0.1",
       {"-m", "mark", "--mark", "0x00002700/0x00003f00", "-j", "RETURN", "-w"}},
      {Iptables::Command::kA,
       "rx_mbimmux0.1",
       {"-m", "mark", "--mark", "0x00002800/0x00003f00", "-j", "RETURN", "-w"}},
      {Iptables::Command::kA, "tx_mbimmux0.1", {"-w"}},
      {Iptables::Command::kA, "rx_mbimmux0.1", {"-w"}},
  };

  for (const auto& rule : expected_calls) {
    EXPECT_CALL(
        datapath_,
        ModifyIptables(IpFamily::kDual, Iptables::Table::kMangle, rule.command,
                       StrEq(rule.chain), ElementsAreArray(rule.argv), _));
  }

  counters_svc_.OnPhysicalDeviceAdded("mbimmux0.1");
}

TEST_F(CountersServiceTest, OnMultiplexedCellularPhysicalDeviceRemoved) {
  const struct {
    Iptables::Command command;
    std::string_view chain;
    std::vector<std::string> argv;
  } expected_calls[] = {
      {Iptables::Command::kD,
       "INPUT",
       {"-i", "mbimmux0.1", "-j", "rx_mbimmux0.1", "-w"}},
      {Iptables::Command::kD,
       "FORWARD",
       {"-i", "mbimmux0.1", "-j", "rx_mbimmux0.1", "-w"}},
      {Iptables::Command::kD,
       "POSTROUTING",
       {"-o", "mbimmux0.1", "-j", "tx_mbimmux0.1", "-w"}},
  };

  for (const auto& rule : expected_calls) {
    EXPECT_CALL(
        datapath_,
        ModifyIptables(IpFamily::kDual, Iptables::Table::kMangle, rule.command,
                       StrEq(rule.chain), ElementsAreArray(rule.argv), _));
  }

  counters_svc_.OnPhysicalDeviceRemoved("mbimmux0.1");
}

TEST_F(CountersServiceTest, OnVpnDeviceAdded) {
  // The following commands are expected when tun0 comes up.
  EXPECT_CALL(datapath_,
              CheckChain(IpFamily::kDual, Iptables::Table::kMangle, "rx_vpn"))
      .WillOnce(Return(false));
  EXPECT_CALL(datapath_,
              CheckChain(IpFamily::kDual, Iptables::Table::kMangle, "tx_vpn"))
      .WillOnce(Return(false));
  EXPECT_CALL(datapath_,
              AddChain(IpFamily::kDual, Iptables::Table::kMangle, "rx_vpn"))
      .WillOnce(Return(true));
  EXPECT_CALL(datapath_,
              AddChain(IpFamily::kDual, Iptables::Table::kMangle, "tx_vpn"))
      .WillOnce(Return(true));
  const struct {
    Iptables::Command command;
    std::string_view chain;
    std::vector<std::string> argv;
  } expected_calls[] = {
      {Iptables::Command::kA,
       "tx_vpn",
       {"-m", "mark", "--mark", "0x00000100/0x00003f00", "-j", "RETURN", "-w"}},
      {Iptables::Command::kA,
       "tx_vpn",
       {"-m", "mark", "--mark", "0x00000200/0x00003f00", "-j", "RETURN", "-w"}},
      {Iptables::Command::kA,
       "tx_vpn",
       {"-m", "mark", "--mark", "0x00000300/0x00003f00", "-j", "RETURN", "-w"}},
      {Iptables::Command::kA,
       "tx_vpn",
       {"-m", "mark", "--mark", "0x00000400/0x00003f00", "-j", "RETURN", "-w"}},
      {Iptables::Command::kA,
       "tx_vpn",
       {"-m", "mark", "--mark", "0x00000500/0x00003f00", "-j", "RETURN", "-w"}},
      {Iptables::Command::kA,
       "tx_vpn",
       {"-m", "mark", "--mark", "0x00002000/0x00003f00", "-j", "RETURN", "-w"}},
      {Iptables::Command::kA,
       "tx_vpn",
       {"-m", "mark", "--mark", "0x00002100/0x00003f00", "-j", "RETURN", "-w"}},
      {Iptables::Command::kA,
       "tx_vpn",
       {"-m", "mark", "--mark", "0x00002200/0x00003f00", "-j", "RETURN", "-w"}},
      {Iptables::Command::kA,
       "tx_vpn",
       {"-m", "mark", "--mark", "0x00002300/0x00003f00", "-j", "RETURN", "-w"}},
      {Iptables::Command::kA,
       "tx_vpn",
       {"-m", "mark", "--mark", "0x00002400/0x00003f00", "-j", "RETURN", "-w"}},
      {Iptables::Command::kA,
       "tx_vpn",
       {"-m", "mark", "--mark", "0x00002500/0x00003f00", "-j", "RETURN", "-w"}},
      {Iptables::Command::kA,
       "tx_vpn",
       {"-m", "mark", "--mark", "0x00002600/0x00003f00", "-j", "RETURN", "-w"}},
      {Iptables::Command::kA,
       "tx_vpn",
       {"-m", "mark", "--mark", "0x00002700/0x00003f00", "-j", "RETURN", "-w"}},
      {Iptables::Command::kA,
       "tx_vpn",
       {"-m", "mark", "--mark", "0x00002800/0x00003f00", "-j", "RETURN", "-w"}},
      {Iptables::Command::kA,
       "rx_vpn",
       {"-m", "mark", "--mark", "0x00000100/0x00003f00", "-j", "RETURN", "-w"}},
      {Iptables::Command::kA,
       "rx_vpn",
       {"-m", "mark", "--mark", "0x00000200/0x00003f00", "-j", "RETURN", "-w"}},
      {Iptables::Command::kA,
       "rx_vpn",
       {"-m", "mark", "--mark", "0x00000300/0x00003f00", "-j", "RETURN", "-w"}},
      {Iptables::Command::kA,
       "rx_vpn",
       {"-m", "mark", "--mark", "0x00000400/0x00003f00", "-j", "RETURN", "-w"}},
      {Iptables::Command::kA,
       "rx_vpn",
       {"-m", "mark", "--mark", "0x00000500/0x00003f00", "-j", "RETURN", "-w"}},
      {Iptables::Command::kA,
       "rx_vpn",
       {"-m", "mark", "--mark", "0x00002000/0x00003f00", "-j", "RETURN", "-w"}},
      {Iptables::Command::kA,
       "rx_vpn",
       {"-m", "mark", "--mark", "0x00002100/0x00003f00", "-j", "RETURN", "-w"}},
      {Iptables::Command::kA,
       "rx_vpn",
       {"-m", "mark", "--mark", "0x00002200/0x00003f00", "-j", "RETURN", "-w"}},
      {Iptables::Command::kA,
       "rx_vpn",
       {"-m", "mark", "--mark", "0x00002300/0x00003f00", "-j", "RETURN", "-w"}},
      {Iptables::Command::kA,
       "rx_vpn",
       {"-m", "mark", "--mark", "0x00002400/0x00003f00", "-j", "RETURN", "-w"}},
      {Iptables::Command::kA,
       "rx_vpn",
       {"-m", "mark", "--mark", "0x00002500/0x00003f00", "-j", "RETURN", "-w"}},
      {Iptables::Command::kA,
       "rx_vpn",
       {"-m", "mark", "--mark", "0x00002600/0x00003f00", "-j", "RETURN", "-w"}},
      {Iptables::Command::kA,
       "rx_vpn",
       {"-m", "mark", "--mark", "0x00002700/0x00003f00", "-j", "RETURN", "-w"}},
      {Iptables::Command::kA,
       "rx_vpn",
       {"-m", "mark", "--mark", "0x00002800/0x00003f00", "-j", "RETURN", "-w"}},
      {Iptables::Command::kA, "tx_vpn", {"-w"}},
      {Iptables::Command::kA, "rx_vpn", {"-w"}},
      {Iptables::Command::kA, "FORWARD", {"-i", "tun0", "-j", "rx_vpn", "-w"}},
      {Iptables::Command::kA, "INPUT", {"-i", "tun0", "-j", "rx_vpn", "-w"}},
      {Iptables::Command::kA,
       "POSTROUTING",
       {"-o", "tun0", "-j", "tx_vpn", "-w"}},
  };

  for (const auto& rule : expected_calls) {
    EXPECT_CALL(
        datapath_,
        ModifyIptables(IpFamily::kDual, Iptables::Table::kMangle, rule.command,
                       StrEq(rule.chain), ElementsAreArray(rule.argv), _));
  }

  counters_svc_.OnVpnDeviceAdded("tun0");
}

TEST_F(CountersServiceTest, OnVpnDeviceRemoved) {
  const struct {
    Iptables::Command command;
    std::string_view chain;
    std::vector<std::string> argv;
  } expected_calls[] = {
      {Iptables::Command::kD, "FORWARD", {"-i", "ppp0", "-j", "rx_vpn", "-w"}},
      {Iptables::Command::kD, "INPUT", {"-i", "ppp0", "-j", "rx_vpn", "-w"}},
      {Iptables::Command::kD,
       "POSTROUTING",
       {"-o", "ppp0", "-j", "tx_vpn", "-w"}},
  };

  for (const auto& rule : expected_calls) {
    EXPECT_CALL(
        datapath_,
        ModifyIptables(IpFamily::kDual, Iptables::Table::kMangle, rule.command,
                       StrEq(rule.chain), ElementsAreArray(rule.argv), _));
  }

  counters_svc_.OnVpnDeviceRemoved("ppp0");
}

TEST_F(CountersServiceTest, OnSameDeviceAppearAgain) {
  // Makes the chain creation commands return false (we already have these
  // rules).
  EXPECT_CALL(datapath_,
              CheckChain(IpFamily::kDual, Iptables::Table::kMangle, _))
      .WillRepeatedly(Return(true));

  // Only the jump rules should be recreated.
  EXPECT_CALL(datapath_, AddChain(IpFamily::kDual, Iptables::Table::kMangle, _))
      .Times(0);
  const struct {
    Iptables::Command command;
    std::string_view chain;
    std::vector<std::string> argv;
  } expected_calls[] = {
      {Iptables::Command::kA, "FORWARD", {"-i", "eth0", "-j", "rx_eth0", "-w"}},
      {Iptables::Command::kA, "INPUT", {"-i", "eth0", "-j", "rx_eth0", "-w"}},
      {Iptables::Command::kA,
       "POSTROUTING",
       {"-o", "eth0", "-j", "tx_eth0", "-w"}},
  };
  for (const auto& rule : expected_calls) {
    EXPECT_CALL(
        datapath_,
        ModifyIptables(IpFamily::kDual, Iptables::Table::kMangle, rule.command,
                       StrEq(rule.chain), ElementsAreArray(rule.argv), _));
  }

  // No fwmark matching rule should be created.
  EXPECT_CALL(datapath_, ModifyIptables(_, Iptables::Table::kMangle, _, _,
                                        Contains("mark"), _))
      .Times(0);

  counters_svc_.OnPhysicalDeviceAdded("eth0");
}

TEST_F(CountersServiceTest, ChainNameLength) {
  // The name of a new chain must be shorter than 29 characters, otherwise
  // iptables will reject the request. Uses Each() here for simplicity since no
  // other params could be longer than 29 for now.
  static constexpr int kMaxChainNameLength = 29;
  EXPECT_CALL(datapath_, ModifyChain(_, Iptables::Table::kMangle, _,
                                     SizeIs(Lt(kMaxChainNameLength)), _))
      .Times(AnyNumber());

  static const std::string kLongInterfaceName(IFNAMSIZ, 'a');
  counters_svc_.OnPhysicalDeviceAdded(kLongInterfaceName);
}

TEST_F(CountersServiceTest, QueryTrafficCounters) {
  EXPECT_CALL(datapath_,
              DumpIptables(IpFamily::kIPv4, Iptables::Table::kMangle))
      .WillOnce(Return(kIptablesOutput));
  EXPECT_CALL(datapath_,
              DumpIptables(IpFamily::kIPv6, Iptables::Table::kMangle))
      .WillOnce(Return(kIp6tablesOutput));

  auto actual = counters_svc_.GetCounters({});

  // The expected counters for eth0 and wlan0. All values are doubled because
  // the same output will be returned for both iptables and ip6tables in the
  // tests.
  std::map<CounterKey, Counter> expected{
      {{"eth0", TrafficCounter::CHROME, TrafficCounter::IPV4},
       {11938 /*rx_bytes*/, 73 /*rx_packets*/, 244427 /*tx_bytes*/,
        1366 /*tx_packets*/}},
      {{"eth0", TrafficCounter::UPDATE_ENGINE, TrafficCounter::IPV4},
       {0 /*rx_bytes*/, 0 /*rx_packets*/, 1670 /*tx_bytes*/,
        20 /*tx_packets*/}},
      {{"eth0", TrafficCounter::SYSTEM, TrafficCounter::IPV4},
       {694 /*rx_bytes*/, 5 /*rx_packets*/, 138402 /*tx_bytes*/,
        550 /*tx_packets*/}},
      {{"eth0", TrafficCounter::ARC, TrafficCounter::IPV4},
       {0 /*rx_bytes*/, 0 /*rx_packets*/, 876172 /*tx_bytes*/,
        5374 /*tx_packets*/}},
      {{"eth0", TrafficCounter::CROSTINI_VM, TrafficCounter::IPV4},
       {0 /*rx_bytes*/, 0 /*rx_packets*/, 2690 /*tx_bytes*/,
        39 /*tx_packets*/}},
      {{"eth0", TrafficCounter::UNKNOWN, TrafficCounter::IPV4},
       {345 /*rx_bytes*/, 6 /*rx_packets*/, 123 /*tx_bytes*/,
        4 /*tx_packets*/}},
      {{"wlan0", TrafficCounter::CHROME, TrafficCounter::IPV4},
       {28098 /*rx_bytes*/, 153 /*rx_packets*/, 57004 /*tx_bytes*/,
        310 /*tx_packets*/}},
      {{"wlan0", TrafficCounter::SYSTEM, TrafficCounter::IPV4},
       {840 /*rx_bytes*/, 6 /*rx_packets*/, 2801 /*tx_bytes*/,
        24 /*tx_packets*/}},
      {{"eth0", TrafficCounter::CHROME, TrafficCounter::IPV6},
       {11938 /*rx_bytes*/, 73 /*rx_packets*/, 244427 /*tx_bytes*/,
        1366 /*tx_packets*/}},
      {{"eth0", TrafficCounter::UPDATE_ENGINE, TrafficCounter::IPV6},
       {0 /*rx_bytes*/, 0 /*rx_packets*/, 1670 /*tx_bytes*/,
        20 /*tx_packets*/}},
      {{"eth0", TrafficCounter::SYSTEM, TrafficCounter::IPV6},
       {694 /*rx_bytes*/, 5 /*rx_packets*/, 138402 /*tx_bytes*/,
        550 /*tx_packets*/}},
      {{"eth0", TrafficCounter::ARC, TrafficCounter::IPV6},
       {0 /*rx_bytes*/, 0 /*rx_packets*/, 876172 /*tx_bytes*/,
        5374 /*tx_packets*/}},
      {{"eth0", TrafficCounter::CROSTINI_VM, TrafficCounter::IPV6},
       {0 /*rx_bytes*/, 0 /*rx_packets*/, 2690 /*tx_bytes*/,
        39 /*tx_packets*/}},
      {{"eth0", TrafficCounter::UNKNOWN, TrafficCounter::IPV6},
       {345 /*rx_bytes*/, 6 /*rx_packets*/, 123 /*tx_bytes*/,
        4 /*tx_packets*/}},
      {{"wlan0", TrafficCounter::CHROME, TrafficCounter::IPV6},
       {28098 /*rx_bytes*/, 153 /*rx_packets*/, 57004 /*tx_bytes*/,
        310 /*tx_packets*/}},
      {{"wlan0", TrafficCounter::SYSTEM, TrafficCounter::IPV6},
       {840 /*rx_bytes*/, 6 /*rx_packets*/, 2801 /*tx_bytes*/,
        24 /*tx_packets*/}},

      {{"mbimmux0.1", TrafficCounter::CHROME, TrafficCounter::IPV6},
       {9763672 /*rx_bytes*/, 9247 /*rx_packets*/, 1178768 /*tx_bytes*/,
        3862 /*tx_packets*/}},
      {{"mbimmux0.1", TrafficCounter::SYSTEM, TrafficCounter::IPV6},
       {72 /*rx_bytes*/, 1 /*rx_packets*/, 12855 /*tx_bytes*/,
        37 /*tx_packets*/}},
      {{"mbimmux0.1", TrafficCounter::ARC, TrafficCounter::IPV6},
       {29640 /*rx_bytes*/, 70 /*rx_packets*/, 11435 /*tx_bytes*/,
        69 /*tx_packets*/}},
      {{"mbimmux0.1", TrafficCounter::UNKNOWN, TrafficCounter::IPV6},
       {960 /*rx_bytes*/, 10 /*rx_packets*/, 0 /*tx_bytes*/, 0 /*tx_packets*/}},
      {{"mbimmux0.1", TrafficCounter::CHROME, TrafficCounter::IPV4},
       {1847697 /*rx_bytes*/, 3607 /*rx_packets*/, 997243 /*tx_bytes*/,
        3221 /*tx_packets*/}},
      {{"mbimmux0.1", TrafficCounter::USER, TrafficCounter::IPV4},
       {31066 /*rx_bytes*/, 180 /*rx_packets*/, 12471 /*tx_bytes*/,
        116 /*tx_packets*/}},
      {{"mbimmux0.1", TrafficCounter::SYSTEM, TrafficCounter::IPV4},
       {25577 /*rx_bytes*/, 69 /*rx_packets*/, 30507 /*tx_bytes*/,
        239 /*tx_packets*/}},
      {{"mbimmux0.1", TrafficCounter::ARC, TrafficCounter::IPV4},
       {61218 /*rx_bytes*/, 152 /*rx_packets*/, 16239 /*tx_bytes*/,
        138 /*tx_packets*/}},
      {{"mbimmux0.1", TrafficCounter::UNKNOWN, TrafficCounter::IPV4},
       {120 /*rx_bytes*/, 3 /*rx_packets*/, 0 /*tx_bytes*/, 0 /*tx_packets*/}},
  };

  EXPECT_TRUE(CompareCounters(expected, actual));
}

TEST_F(CountersServiceTest, QueryTrafficCountersWithFilter) {
  EXPECT_CALL(datapath_,
              DumpIptables(IpFamily::kIPv4, Iptables::Table::kMangle))
      .WillOnce(Return(kIptablesOutput));
  EXPECT_CALL(datapath_,
              DumpIptables(IpFamily::kIPv6, Iptables::Table::kMangle))
      .WillOnce(Return(kIp6tablesOutput));

  // Only counters for eth0 should be returned. eth1 should be ignored.
  auto actual = counters_svc_.GetCounters({"eth0", "eth1"});

  // The expected counters for eth0. All values are doubled because
  // the same output will be returned for both iptables and ip6tables in the
  // tests.
  std::map<CounterKey, Counter> expected{
      {{"eth0", TrafficCounter::CHROME, TrafficCounter::IPV4},
       {11938 /*rx_bytes*/, 73 /*rx_packets*/, 244427 /*tx_bytes*/,
        1366 /*tx_packets*/}},
      {{"eth0", TrafficCounter::UPDATE_ENGINE, TrafficCounter::IPV4},
       {0 /*rx_bytes*/, 0 /*rx_packets*/, 1670 /*tx_bytes*/,
        20 /*tx_packets*/}},
      {{"eth0", TrafficCounter::SYSTEM, TrafficCounter::IPV4},
       {694 /*rx_bytes*/, 5 /*rx_packets*/, 138402 /*tx_bytes*/,
        550 /*tx_packets*/}},
      {{"eth0", TrafficCounter::ARC, TrafficCounter::IPV4},
       {0 /*rx_bytes*/, 0 /*rx_packets*/, 876172 /*tx_bytes*/,
        5374 /*tx_packets*/}},
      {{"eth0", TrafficCounter::CROSTINI_VM, TrafficCounter::IPV4},
       {0 /*rx_bytes*/, 0 /*rx_packets*/, 2690 /*tx_bytes*/,
        39 /*tx_packets*/}},
      {{"eth0", TrafficCounter::UNKNOWN, TrafficCounter::IPV4},
       {345 /*rx_bytes*/, 6 /*rx_packets*/, 123 /*tx_bytes*/,
        4 /*tx_packets*/}},
      {{"eth0", TrafficCounter::CHROME, TrafficCounter::IPV6},
       {11938 /*rx_bytes*/, 73 /*rx_packets*/, 244427 /*tx_bytes*/,
        1366 /*tx_packets*/}},
      {{"eth0", TrafficCounter::UPDATE_ENGINE, TrafficCounter::IPV6},
       {0 /*rx_bytes*/, 0 /*rx_packets*/, 1670 /*tx_bytes*/,
        20 /*tx_packets*/}},
      {{"eth0", TrafficCounter::SYSTEM, TrafficCounter::IPV6},
       {694 /*rx_bytes*/, 5 /*rx_packets*/, 138402 /*tx_bytes*/,
        550 /*tx_packets*/}},
      {{"eth0", TrafficCounter::ARC, TrafficCounter::IPV6},
       {0 /*rx_bytes*/, 0 /*rx_packets*/, 876172 /*tx_bytes*/,
        5374 /*tx_packets*/}},
      {{"eth0", TrafficCounter::CROSTINI_VM, TrafficCounter::IPV6},
       {0 /*rx_bytes*/, 0 /*rx_packets*/, 2690 /*tx_bytes*/,
        39 /*tx_packets*/}},
      {{"eth0", TrafficCounter::UNKNOWN, TrafficCounter::IPV6},
       {345 /*rx_bytes*/, 6 /*rx_packets*/, 123 /*tx_bytes*/,
        4 /*tx_packets*/}},
  };

  EXPECT_TRUE(CompareCounters(expected, actual));
}

TEST_F(CountersServiceTest, QueryTraffic_UnknownTrafficOnly) {
  const std::string unknown_ipv4_traffic_only = R"(
Chain tx_eth0 (1 references)
    pkts      bytes target     prot opt in     out     source               destination
    6511 68041668            all  --  *    *     0.0.0.0/0             0.0.0.0/0
)";

  const std::string unknown_ipv6_traffic_only = R"(
Chain tx_eth0 (1 references)
    pkts      bytes target     prot opt in     out     source               destination
    211 13456            all  --  any    any     ::/0             ::/0
)";

  EXPECT_CALL(datapath_,
              DumpIptables(IpFamily::kIPv4, Iptables::Table::kMangle))
      .WillOnce(Return(unknown_ipv4_traffic_only));
  EXPECT_CALL(datapath_,
              DumpIptables(IpFamily::kIPv6, Iptables::Table::kMangle))
      .WillOnce(Return(unknown_ipv6_traffic_only));

  auto actual = counters_svc_.GetCounters({});

  std::map<CounterKey, Counter> expected{
      {{"eth0", TrafficCounter::UNKNOWN, TrafficCounter::IPV4},
       {0 /*rx_bytes*/, 0 /*rx_packets*/, 68041668 /*tx_bytes*/,
        6511 /*tx_packets*/}},
      {{"eth0", TrafficCounter::UNKNOWN, TrafficCounter::IPV6},
       {0 /*rx_bytes*/, 0 /*rx_packets*/, 13456 /*tx_bytes*/,
        211 /*tx_packets*/}},
  };

  EXPECT_TRUE(CompareCounters(expected, actual));
}

TEST_F(CountersServiceTest, QueryTrafficCountersWithEmptyIPv4Output) {
  TestBadIptablesOutput("", kIp6tablesOutput);
}

TEST_F(CountersServiceTest, QueryTrafficCountersWithEmptyIPv6Output) {
  TestBadIptablesOutput(kIptablesOutput, "");
}

TEST_F(CountersServiceTest, QueryTrafficCountersWithOnlyChainName) {
  const std::string kBadOutput = R"(
Chain tx_eth0 (1 references)
    pkts      bytes target     prot opt in     out     source               destination
    6511 68041668 RETURN    all  --  *    *     0.0.0.0/0             0.0.0.0/0

Chain tx_wlan0 (1 references)
)";
  TestBadIptablesOutput(kBadOutput, kIp6tablesOutput);
}

TEST_F(CountersServiceTest, QueryTrafficCountersWithOnlyChainNameAndHeader) {
  const std::string kBadOutput = R"(
Chain tx_eth0 (1 references)
    pkts      bytes target     prot opt in     out     source               destination
    6511 68041668 RETURN    all  --  *    *     0.0.0.0/0             0.0.0.0/0

Chain tx_wlan0 (1 references)
    pkts      bytes target     prot opt in     out     source               destination
)";
  TestBadIptablesOutput(kBadOutput, kIp6tablesOutput);
}

TEST_F(CountersServiceTest, QueryTrafficCountersWithNotFinishedCountersLine) {
  const std::string kBadOutput = R"(
Chain tx_eth0 (1 references)
    pkts      bytes target     prot opt in     out     source               destination
    6511 68041668 RETURN    all  --  *    *     0.0.0.0/0             0.0.0.0/0

Chain tx_wlan0 (1 references)
    pkts      bytes target     prot opt in     out     source               destination    pkts      bytes target     prot opt in     out     source               destination
       0     )";
  TestBadIptablesOutput(kBadOutput, kIp6tablesOutput);
}

TEST_F(CountersServiceTest, HandleARCVPNSocketConnectionEvent) {
  auto updater = std::make_unique<MockConnmarkUpdater>(&conntrack_monitor_);
  auto updater_ptr = updater.get();
  counters_svc_.SetConnmarkUpdaterForTesting(std::move(updater));

  std::unique_ptr<patchpanel::SocketConnectionEvent> msg =
      std::make_unique<patchpanel::SocketConnectionEvent>();
  net_base::IPv4Address src_addr =
      *net_base::IPv4Address::CreateFromString(kIPAddress1);
  msg->set_saddr(src_addr.ToByteString());
  msg->set_sport(kPort1);
  msg->set_dport(kPort2);
  msg->set_proto(patchpanel::SocketConnectionEvent::IpProtocol::
                     SocketConnectionEvent_IpProtocol_TCP);

  // Test that when destination address is not set in the
  // SocketConnectionEvent, ConnmarkUpdater is not called.
  EXPECT_CALL(*updater_ptr, UpdateConnmark).Times(0);
  counters_svc_.HandleARCVPNSocketConnectionEvent(*msg);
  Mock::VerifyAndClearExpectations(updater_ptr);

  // Test that when IP protocol of the SocketConnectionEvent is not TCP or UDP,
  // ConnmarkUpdater is not called.
  net_base::IPv4Address dst_addr =
      *net_base::IPv4Address::CreateFromString(kIPAddress2);
  msg->set_daddr(dst_addr.ToByteString());
  msg->set_proto(patchpanel::SocketConnectionEvent::IpProtocol::
                     SocketConnectionEvent_IpProtocol_UNKNOWN_PROTO);
  EXPECT_CALL(*updater_ptr, UpdateConnmark).Times(0);
  counters_svc_.HandleARCVPNSocketConnectionEvent(*msg);
  Mock::VerifyAndClearExpectations(updater_ptr);

  // Test that ConnmarkUpdater is called with correct args when valid
  // SocketConnectionEvent is passed in.
  auto tcp_conn = ConnmarkUpdater::Conntrack5Tuple{
      .src_addr = *(net_base::IPAddress::CreateFromString(kIPAddress1)),
      .dst_addr = *(net_base::IPAddress::CreateFromString(kIPAddress2)),
      .sport = static_cast<uint16_t>(kPort1),
      .dport = static_cast<uint16_t>(kPort2),
      .proto = ConnmarkUpdater::IPProtocol::kTCP};
  msg->set_proto(patchpanel::SocketConnectionEvent::IpProtocol::
                     SocketConnectionEvent_IpProtocol_TCP);
  EXPECT_CALL(
      *updater_ptr,
      UpdateConnmark(Eq(tcp_conn), Fwmark::FromSource(TrafficSource::kArcVpn),
                     kFwmarkAllSourcesMask));
  counters_svc_.HandleARCVPNSocketConnectionEvent(*msg);
  Mock::VerifyAndClearExpectations(updater_ptr);
}

}  // namespace
}  // namespace patchpanel
