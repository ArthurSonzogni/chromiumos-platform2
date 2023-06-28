// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/multicast_counters_service.h"

#include <string>
#include <vector>

#include "base/strings/string_piece.h"
#include <base/logging.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "patchpanel/iptables.h"
#include "patchpanel/mock_datapath.h"
#include "patchpanel/multicast_forwarder.h"

namespace patchpanel {

using ::testing::_;
using ::testing::ElementsAreArray;
using ::testing::Return;
using ::testing::StrEq;

using CounterKey = std::pair<MulticastCountersService::MulticastProtocolType,
                             MulticastCountersService::MulticastTechnologyType>;

namespace {
// The following strings are copied from the real output of iptables v1.8.5 by
// `iptables -t mangle -L -x -v -n` and `ip6tables -t mangle -L -x -v -n`.
// This output contains all the accounting chains/rules for eth0 and wlan0.
// Packet number for multicast traffic modified for testing reason.
const char kIptablesOutput[] = R"(
Chain PREROUTING (policy ACCEPT 8949 packets, 872859 bytes)
    pkts      bytes target     prot opt in     out     source               destination         
    9109   892835 CONNMARK   all  --  eth0   *       0.0.0.0/0            0.0.0.0/0            CONNMARK restore mask 0x3f00
       0        0 CONNMARK   all  --  wlan0  *       0.0.0.0/0            0.0.0.0/0            CONNMARK restore mask 0x3f00

Chain INPUT (policy ACCEPT 8941 packets, 871259 bytes)
    pkts      bytes target     prot opt in     out     source               destination         
    8870   805689 rx_mdns    udp  --  *      *       0.0.0.0/0            224.0.0.251          udp dpt:5353
       0        0 rx_ssdp    udp  --  *      *       0.0.0.0/0            239.255.255.250      udp dpt:1900
    9090   888396 rx_eth0    all  --  eth0   *       0.0.0.0/0            0.0.0.0/0           
       0        0 rx_wlan0   all  --  wlan0  *       0.0.0.0/0            0.0.0.0/0

Chain FORWARD (policy ACCEPT 8 packets, 1600 bytes)
    pkts      bytes target     prot opt in     out     source               destination         
       8     2588 rx_eth0    all  --  eth0   *       0.0.0.0/0            0.0.0.0/0           
       0        0 rx_wlan0   all  --  wlan0  *       0.0.0.0/0            0.0.0.0/0 

Chain OUTPUT (policy ACCEPT 4574 packets, 2900995 bytes)
    pkts      bytes target     prot opt in     out     source               destination

Chain POSTROUTING (policy ACCEPT 267 packets, 46534 bytes)
    pkts      bytes target     prot opt in     out     source               destination         
     197    31686 tx_eth0    all  --  *      eth0    0.0.0.0/0            0.0.0.0/0           
     197    31686 POSTROUTING_eth0  all  --  *      eth0    0.0.0.0/0            0.0.0.0/0           
       0        0 tx_wlan0   all  --  *      wlan0   0.0.0.0/0            0.0.0.0/0           
       0        0 POSTROUTING_wlan0  all  --  *      wlan0   0.0.0.0/0            0.0.0.0/0

Chain POSTROUTING_eth0 (1 references)
    pkts      bytes target     prot opt in     out     source               destination         
     197    31686 CONNMARK   all  --  *      *       0.0.0.0/0            0.0.0.0/0            CONNMARK xset 0x3ea0000/0xffff0000
     197    31686 CONNMARK   all  --  *      *       0.0.0.0/0            0.0.0.0/0            CONNMARK save mask 0x3f00

Chain POSTROUTING_wlan0 (1 references)
    pkts      bytes target     prot opt in     out     source               destination         
       0        0 CONNMARK   all  --  *      *       0.0.0.0/0            0.0.0.0/0            CONNMARK xset 0x3eb0000/0xffff0000
       0        0 CONNMARK   all  --  *      *       0.0.0.0/0            0.0.0.0/0            CONNMARK save mask 0x3f00

Chain rx_eth0 (2 references)
    pkts      bytes target     prot opt in     out     source               destination         
     100    56190 RETURN     all  --  *      *       0.0.0.0/0            0.0.0.0/0            mark match 0x100/0x3f00
       6     1964 RETURN     all  --  *      *       0.0.0.0/0            0.0.0.0/0            mark match 0x200/0x3f00
       0        0 RETURN     all  --  *      *       0.0.0.0/0            0.0.0.0/0            mark match 0x300/0x3f00
      88    25815 RETURN     all  --  *      *       0.0.0.0/0            0.0.0.0/0            mark match 0x400/0x3f00
       0        0 RETURN     all  --  *      *       0.0.0.0/0            0.0.0.0/0            mark match 0x500/0x3f00
       0        0 RETURN     all  --  *      *       0.0.0.0/0            0.0.0.0/0            mark match 0x2000/0x3f00
       0        0 RETURN     all  --  *      *       0.0.0.0/0            0.0.0.0/0            mark match 0x2100/0x3f00
       0        0 RETURN     all  --  *      *       0.0.0.0/0            0.0.0.0/0            mark match 0x2200/0x3f00
       0        0 RETURN     all  --  *      *       0.0.0.0/0            0.0.0.0/0            mark match 0x2300/0x3f00
       0        0 RETURN     all  --  *      *       0.0.0.0/0            0.0.0.0/0            mark match 0x2400/0x3f00
    8904   807015            all  --  *      *       0.0.0.0/0            0.0.0.0/0           

Chain rx_ethernet_mdns (1 references)
    pkts      bytes target     prot opt in     out     source               destination         

Chain rx_ethernet_ssdp (1 references)
    pkts      bytes target     prot opt in     out     source               destination         

Chain rx_mdns (1 references)
    pkts      bytes target     prot opt in     out     source               destination         
    8867   805299 rx_ethernet_mdns  all  --  eth0   *       0.0.0.0/0            0.0.0.0/0           
       0        0 rx_wifi_mdns  all  --  wlan0  *       0.0.0.0/0            0.0.0.0/0           

Chain rx_ssdp (1 references)
    pkts      bytes target     prot opt in     out     source               destination         
       0        0 rx_ethernet_ssdp  all  --  eth0   *       0.0.0.0/0            0.0.0.0/0           
       0        0 rx_wifi_ssdp  all  --  wlan0  *       0.0.0.0/0            0.0.0.0/0           

Chain rx_wifi_mdns (1 references)
    pkts      bytes target     prot opt in     out     source               destination         

Chain rx_wifi_ssdp (1 references)
    pkts      bytes target     prot opt in     out     source               destination         

Chain rx_wlan0 (2 references)
    pkts      bytes target     prot opt in     out     source               destination         
       0        0 RETURN     all  --  *      *       0.0.0.0/0            0.0.0.0/0            mark match 0x100/0x3f00
       0        0 RETURN     all  --  *      *       0.0.0.0/0            0.0.0.0/0            mark match 0x200/0x3f00
       0        0 RETURN     all  --  *      *       0.0.0.0/0            0.0.0.0/0            mark match 0x300/0x3f00
       0        0 RETURN     all  --  *      *       0.0.0.0/0            0.0.0.0/0            mark match 0x400/0x3f00
       0        0 RETURN     all  --  *      *       0.0.0.0/0            0.0.0.0/0            mark match 0x500/0x3f00
       0        0 RETURN     all  --  *      *       0.0.0.0/0            0.0.0.0/0            mark match 0x2000/0x3f00
       0        0 RETURN     all  --  *      *       0.0.0.0/0            0.0.0.0/0            mark match 0x2100/0x3f00
       0        0 RETURN     all  --  *      *       0.0.0.0/0            0.0.0.0/0            mark match 0x2200/0x3f00
       0        0 RETURN     all  --  *      *       0.0.0.0/0            0.0.0.0/0            mark match 0x2300/0x3f00
       0        0 RETURN     all  --  *      *       0.0.0.0/0            0.0.0.0/0            mark match 0x2400/0x3f00
       0        0            all  --  *      *       0.0.0.0/0            0.0.0.0/0           

Chain tx_eth0 (1 references)
    pkts      bytes target     prot opt in     out     source               destination         
     100    18485 RETURN     all  --  *      *       0.0.0.0/0            0.0.0.0/0            mark match 0x100/0x3f00
       6      406 RETURN     all  --  *      *       0.0.0.0/0            0.0.0.0/0            mark match 0x200/0x3f00
       0        0 RETURN     all  --  *      *       0.0.0.0/0            0.0.0.0/0            mark match 0x300/0x3f00
      91    12795 RETURN     all  --  *      *       0.0.0.0/0            0.0.0.0/0            mark match 0x400/0x3f00
       0        0 RETURN     all  --  *      *       0.0.0.0/0            0.0.0.0/0            mark match 0x500/0x3f00
       0        0 RETURN     all  --  *      *       0.0.0.0/0            0.0.0.0/0            mark match 0x2000/0x3f00
       0        0 RETURN     all  --  *      *       0.0.0.0/0            0.0.0.0/0            mark match 0x2100/0x3f00
       0        0 RETURN     all  --  *      *       0.0.0.0/0            0.0.0.0/0            mark match 0x2200/0x3f00
       0        0 RETURN     all  --  *      *       0.0.0.0/0            0.0.0.0/0            mark match 0x2300/0x3f00
       0        0 RETURN     all  --  *      *       0.0.0.0/0            0.0.0.0/0            mark match 0x2400/0x3f00
       0        0            all  --  *      *       0.0.0.0/0            0.0.0.0/0           

Chain tx_wlan0 (1 references)
    pkts      bytes target     prot opt in     out     source               destination         
       0        0 RETURN     all  --  *      *       0.0.0.0/0            0.0.0.0/0            mark match 0x100/0x3f00
       0        0 RETURN     all  --  *      *       0.0.0.0/0            0.0.0.0/0            mark match 0x200/0x3f00
       0        0 RETURN     all  --  *      *       0.0.0.0/0            0.0.0.0/0            mark match 0x300/0x3f00
       0        0 RETURN     all  --  *      *       0.0.0.0/0            0.0.0.0/0            mark match 0x400/0x3f00
       0        0 RETURN     all  --  *      *       0.0.0.0/0            0.0.0.0/0            mark match 0x500/0x3f00
       0        0 RETURN     all  --  *      *       0.0.0.0/0            0.0.0.0/0            mark match 0x2000/0x3f00
       0        0 RETURN     all  --  *      *       0.0.0.0/0            0.0.0.0/0            mark match 0x2100/0x3f00
       0        0 RETURN     all  --  *      *       0.0.0.0/0            0.0.0.0/0            mark match 0x2200/0x3f00
       0        0 RETURN     all  --  *      *       0.0.0.0/0            0.0.0.0/0            mark match 0x2300/0x3f00
       0        0 RETURN     all  --  *      *       0.0.0.0/0            0.0.0.0/0            mark match 0x2400/0x3f00
       0        0            all  --  *      *       0.0.0.0/0            0.0.0.0/0           
)";

const char kIp6tablesOutput[] = R"(
Chain PREROUTING (policy ACCEPT 98844 packets, 15455949 bytes)
    pkts      bytes target     prot opt in     out     source               destination         
   99159 15483417 CONNMARK   all      eth0   *       ::/0                 ::/0                 CONNMARK restore mask 0x3f00
       0        0 CONNMARK   all      wlan0  *       ::/0                 ::/0                 CONNMARK restore mask 0x3f00

Chain INPUT (policy ACCEPT 16143 packets, 904568 bytes)
    pkts      bytes target     prot opt in     out     source               destination         
    1500     3000 rx_mdns    udp      *      *       ::/0                 ff02::fb             udp dpt:5353
    200       400 rx_ssdp    udp      *      *       ::/0                 ff02::c              udp dpt:1900
   16342   915744 rx_eth0    all      eth0   *       ::/0                 ::/0                
       0        0 rx_wlan0   all      wlan0  *       ::/0                 ::/0 

Chain FORWARD (policy ACCEPT 0 packets, 0 bytes)
    pkts      bytes target     prot opt in     out     source               destination         
       0        0 rx_eth0    all      eth0   *       ::/0                 ::/0                
       0        0 rx_wlan0   all      wlan0  *       ::/0                 ::/0

Chain OUTPUT (policy ACCEPT 4574 packets, 2900995 bytes)
    pkts      bytes target     prot opt in     out     source               destination

Chain POSTROUTING (policy ACCEPT 5 packets, 280 bytes)
    pkts      bytes target     prot opt in     out     source               destination         
      16     1312 tx_eth0    all      *      eth0    ::/0                 ::/0                
      16     1312 POSTROUTING_eth0  all      *      eth0    ::/0                 ::/0                
       0        0 tx_wlan0   all      *      wlan0   ::/0                 ::/0                
       0        0 POSTROUTING_wlan0  all      *      wlan0   ::/0                 ::/0 

Chain POSTROUTING_eth0 (1 references)
    pkts      bytes target     prot opt in     out     source               destination         
      16     1312 CONNMARK   all      *      *       ::/0                 ::/0                 CONNMARK xset 0x3ea0000/0xffff0000
      16     1312 CONNMARK   all      *      *       ::/0                 ::/0                 CONNMARK save mask 0x3f00

Chain POSTROUTING_wlan0 (1 references)
    pkts      bytes target     prot opt in     out     source               destination         
       0        0 CONNMARK   all      *      *       ::/0                 ::/0                 CONNMARK xset 0x3eb0000/0xffff0000
       0        0 CONNMARK   all      *      *       ::/0                 ::/0                 CONNMARK save mask 0x3f00

Chain rx_eth0 (2 references)
    pkts      bytes target     prot opt in     out     source               destination         
       0        0 RETURN     all      *      *       ::/0                 ::/0                 mark match 0x100/0x3f00
       0        0 RETURN     all      *      *       ::/0                 ::/0                 mark match 0x200/0x3f00
       0        0 RETURN     all      *      *       ::/0                 ::/0                 mark match 0x300/0x3f00
       0        0 RETURN     all      *      *       ::/0                 ::/0                 mark match 0x400/0x3f00
       0        0 RETURN     all      *      *       ::/0                 ::/0                 mark match 0x500/0x3f00
       0        0 RETURN     all      *      *       ::/0                 ::/0                 mark match 0x2000/0x3f00
       0        0 RETURN     all      *      *       ::/0                 ::/0                 mark match 0x2100/0x3f00
       0        0 RETURN     all      *      *       ::/0                 ::/0                 mark match 0x2200/0x3f00
       0        0 RETURN     all      *      *       ::/0                 ::/0                 mark match 0x2300/0x3f00
       0        0 RETURN     all      *      *       ::/0                 ::/0                 mark match 0x2400/0x3f00
   16342   915744            all      *      *       ::/0                 ::/0                

Chain rx_ethernet_mdns (1 references)
    pkts      bytes target     prot opt in     out     source               destination         

Chain rx_ethernet_ssdp (1 references)
    pkts      bytes target     prot opt in     out     source               destination         

Chain rx_mdns (1 references)
    pkts      bytes target     prot opt in     out     source               destination         
    1000       2000 rx_ethernet_mdns  all      eth0   *       ::/0                 ::/0                
    500        1000 rx_wifi_mdns  all      wlan0  *       ::/0                 ::/0                

Chain rx_ssdp (1 references)
    pkts      bytes target     prot opt in     out     source               destination         
      150      300 rx_ethernet_ssdp  all      eth0   *       ::/0                 ::/0                
       50      100 rx_wifi_ssdp  all      wlan0  *       ::/0                 ::/0                

Chain rx_wifi_mdns (1 references)
    pkts      bytes target     prot opt in     out     source               destination         

Chain rx_wifi_ssdp (1 references)
    pkts      bytes target     prot opt in     out     source               destination         

Chain rx_wlan0 (2 references)
    pkts      bytes target     prot opt in     out     source               destination         
       0        0 RETURN     all      *      *       ::/0                 ::/0                 mark match 0x100/0x3f00
       0        0 RETURN     all      *      *       ::/0                 ::/0                 mark match 0x200/0x3f00
       0        0 RETURN     all      *      *       ::/0                 ::/0                 mark match 0x300/0x3f00
       0        0 RETURN     all      *      *       ::/0                 ::/0                 mark match 0x400/0x3f00
       0        0 RETURN     all      *      *       ::/0                 ::/0                 mark match 0x500/0x3f00
       0        0 RETURN     all      *      *       ::/0                 ::/0                 mark match 0x2000/0x3f00
       0        0 RETURN     all      *      *       ::/0                 ::/0                 mark match 0x2100/0x3f00
       0        0 RETURN     all      *      *       ::/0                 ::/0                 mark match 0x2200/0x3f00
       0        0 RETURN     all      *      *       ::/0                 ::/0                 mark match 0x2300/0x3f00
       0        0 RETURN     all      *      *       ::/0                 ::/0                 mark match 0x2400/0x3f00
       0        0            all      *      *       ::/0                 ::/0                

Chain tx_eth0 (1 references)
    pkts      bytes target     prot opt in     out     source               destination         
       0        0 RETURN     all      *      *       ::/0                 ::/0                 mark match 0x100/0x3f00
       0        0 RETURN     all      *      *       ::/0                 ::/0                 mark match 0x200/0x3f00
       0        0 RETURN     all      *      *       ::/0                 ::/0                 mark match 0x300/0x3f00
       8      848 RETURN     all      *      *       ::/0                 ::/0                 mark match 0x400/0x3f00
       0        0 RETURN     all      *      *       ::/0                 ::/0                 mark match 0x500/0x3f00
       0        0 RETURN     all      *      *       ::/0                 ::/0                 mark match 0x2000/0x3f00
       0        0 RETURN     all      *      *       ::/0                 ::/0                 mark match 0x2100/0x3f00
       0        0 RETURN     all      *      *       ::/0                 ::/0                 mark match 0x2200/0x3f00
       0        0 RETURN     all      *      *       ::/0                 ::/0                 mark match 0x2300/0x3f00
       0        0 RETURN     all      *      *       ::/0                 ::/0                 mark match 0x2400/0x3f00
       8      464            all      *      *       ::/0                 ::/0                

Chain tx_wlan0 (1 references)
    pkts      bytes target     prot opt in     out     source               destination         
       0        0 RETURN     all      *      *       ::/0                 ::/0                 mark match 0x100/0x3f00
       0        0 RETURN     all      *      *       ::/0                 ::/0                 mark match 0x200/0x3f00
       0        0 RETURN     all      *      *       ::/0                 ::/0                 mark match 0x300/0x3f00
       0        0 RETURN     all      *      *       ::/0                 ::/0                 mark match 0x400/0x3f00
       0        0 RETURN     all      *      *       ::/0                 ::/0                 mark match 0x500/0x3f00
       0        0 RETURN     all      *      *       ::/0                 ::/0                 mark match 0x2000/0x3f00
       0        0 RETURN     all      *      *       ::/0                 ::/0                 mark match 0x2100/0x3f00
       0        0 RETURN     all      *      *       ::/0                 ::/0                 mark match 0x2200/0x3f00
       0        0 RETURN     all      *      *       ::/0                 ::/0                 mark match 0x2300/0x3f00
       0        0 RETURN     all      *      *       ::/0                 ::/0                 mark match 0x2400/0x3f00
       0        0            all      *      *       ::/0                 ::/0                
)";

std::optional<std::string> CounterKeyToString(const CounterKey& key) {
  std::string technology;
  std::string protocol;
  if (key.first == MulticastCountersService::MulticastProtocolType::kMdns) {
    protocol = "mdns";
  } else if (key.first ==
             MulticastCountersService::MulticastProtocolType::kSsdp) {
    protocol = "ssdp";
  } else {
    return {};
  }

  if (key.second ==
      MulticastCountersService::MulticastTechnologyType::kEthernet) {
    technology = "ethernet";
  } else if (key.second ==
             MulticastCountersService::MulticastTechnologyType::kWifi) {
    technology = "wifi";
  } else {
    return {};
  }
  return "protocol:" + protocol + " / technology:" + technology;
}

bool CompareCounters(std::map<CounterKey, uint64_t> expected,
                     std::map<CounterKey, uint64_t> actual) {
  for (const auto& kv : expected) {
    if (!CounterKeyToString(kv.first).has_value()) {
      LOG(ERROR) << "CounterKey is not valid in expected counter";
      return false;
    }
    const auto it = actual.find(kv.first);
    if (it == actual.end()) {
      LOG(ERROR) << "Could not find expected CounterKey="
                 << CounterKeyToString(kv.first).value();
      return false;
    }
    if (!(it->second == kv.second)) {
      LOG(ERROR) << "Unexpected packet number=" << it->second
                 << " for CounterKey=" << CounterKeyToString(kv.first).value()
                 << ". Expected instead " << kv.second;
      return false;
    }
  }
  for (const auto& kv : actual) {
    if (expected.find(kv.first) == expected.end()) {
      if (!CounterKeyToString(kv.first).has_value()) {
        LOG(ERROR) << "CounterKey is not valid in actual counter";
        return false;
      }
      LOG(ERROR) << "Unexpected entry CounterKey="
                 << CounterKeyToString(kv.first).value()
                 << " packet number=" << kv.second;
      return false;
    }
  }
  return true;
}

bool IsEmptyCounters(std::map<CounterKey, uint64_t> counter) {
  std::vector<MulticastCountersService::CounterKey> expected_keys = {
      {MulticastCountersService::MulticastProtocolType::kMdns,
       MulticastCountersService::MulticastTechnologyType::kEthernet},
      {MulticastCountersService::MulticastProtocolType::kSsdp,
       MulticastCountersService::MulticastTechnologyType::kEthernet},
      {MulticastCountersService::MulticastProtocolType::kMdns,
       MulticastCountersService::MulticastTechnologyType::kWifi},
      {MulticastCountersService::MulticastProtocolType::kSsdp,
       MulticastCountersService::MulticastTechnologyType::kWifi},
  };
  for (const auto& key : expected_keys) {
    const auto it = counter.find(key);
    if (it == counter.end()) {
      return false;
    }
    if (it->second != 0) {
      return false;
    }
  }
  return true;
}

ShillClient::Device MakeShillDevice(ShillClient::Device::Type type,
                                    base::StringPiece ifname,
                                    int ifindex) {
  ShillClient::Device dev;
  dev.type = type;
  dev.shill_device_interface_property = ifname;
  dev.ifname = ifname;
  dev.ifindex = ifindex;
  return dev;
}

class MulticastCountersServiceTest : public testing::Test {
 protected:
  void SetUp() override {
    datapath_ = std::make_unique<MockDatapath>();
    multicast_counters_svc_ =
        std::make_unique<MulticastCountersService>(datapath_.get());
  }

  std::unique_ptr<MockDatapath> datapath_;
  std::unique_ptr<MulticastCountersService> multicast_counters_svc_;
};

TEST_F(MulticastCountersServiceTest, StartMulticastCountersService) {
  static const struct {
    IpFamily ip_family;
    std::string chain;
  } expected_chain_creations[] = {
      {IpFamily::kDual, "rx_mdns"},
      {IpFamily::kDual, "rx_ssdp"},
      {IpFamily::kDual, "rx_ethernet_mdns"},
      {IpFamily::kDual, "rx_ethernet_ssdp"},
      {IpFamily::kDual, "rx_wifi_mdns"},
      {IpFamily::kDual, "rx_wifi_ssdp"},
  };
  static const struct {
    IpFamily ip_family;
    Iptables::Command command;
    std::vector<std::string> argv;
  } expected_rule_creations[] = {
      {IpFamily::kIPv4,
       Iptables::Command::kA,
       {"-d", kMdnsMcastAddress.ToString(), "-p", "udp", "--dport", "5353",
        "-j", "rx_mdns"}},
      {IpFamily::kIPv4,
       Iptables::Command::kA,
       {"-d", kSsdpMcastAddress.ToString(), "-p", "udp", "--dport", "1900",
        "-j", "rx_ssdp"}},
      {IpFamily::kIPv6,
       Iptables::Command::kA,
       {"-d", kMdnsMcastAddress6.ToString(), "-p", "udp", "--dport", "5353",
        "-j", "rx_mdns"}},
      {IpFamily::kIPv6,
       Iptables::Command::kA,
       {"-d", kSsdpMcastAddress6.ToString(), "-p", "udp", "--dport", "1900",
        "-j", "rx_ssdp"}},
  };

  for (const auto& rule : expected_chain_creations) {
    EXPECT_CALL(*datapath_,
                AddChain(rule.ip_family, Iptables::Table::kMangle, rule.chain));
  }
  for (const auto& rule : expected_rule_creations) {
    EXPECT_CALL(
        *datapath_,
        ModifyIptables(rule.ip_family, Iptables::Table::kMangle, rule.command,
                       StrEq("INPUT"), ElementsAreArray(rule.argv), _));
  }

  multicast_counters_svc_->Start();
}

TEST_F(MulticastCountersServiceTest, StopMulticastCountersService) {
  static const struct {
    IpFamily ip_family;
    Iptables::Command command;
    std::vector<std::string> argv;
  } expected_rule_deletions[] = {
      {IpFamily::kIPv4,
       Iptables::Command::kD,
       {"-d", kMdnsMcastAddress.ToString(), "-p", "udp", "--dport", "5353",
        "-j", "rx_mdns"}},
      {IpFamily::kIPv4,
       Iptables::Command::kD,
       {"-d", kSsdpMcastAddress.ToString(), "-p", "udp", "--dport", "1900",
        "-j", "rx_ssdp"}},
      {IpFamily::kIPv6,
       Iptables::Command::kD,
       {"-d", kMdnsMcastAddress6.ToString(), "-p", "udp", "--dport", "5353",
        "-j", "rx_mdns"}},
      {IpFamily::kIPv6,
       Iptables::Command::kD,
       {"-d", kSsdpMcastAddress6.ToString(), "-p", "udp", "--dport", "1900",
        "-j", "rx_ssdp"}},
  };
  static const struct {
    IpFamily ip_family;
    std::string chain;
  } expected_chain_flushes[] = {
      {IpFamily::kDual, "rx_mdns"},
      {IpFamily::kDual, "rx_ssdp"},
  };
  static const struct {
    IpFamily ip_family;
    std::string chain;
  } expected_chain_deletions[] = {
      {IpFamily::kDual, "rx_ethernet_mdns"},
      {IpFamily::kDual, "rx_ethernet_ssdp"},
      {IpFamily::kDual, "rx_wifi_mdns"},
      {IpFamily::kDual, "rx_wifi_ssdp"},
      {IpFamily::kDual, "rx_mdns"},
      {IpFamily::kDual, "rx_ssdp"},
  };

  for (const auto& rule : expected_rule_deletions) {
    EXPECT_CALL(
        *datapath_,
        ModifyIptables(rule.ip_family, Iptables::Table::kMangle, rule.command,
                       StrEq("INPUT"), ElementsAreArray(rule.argv), _));
  }
  for (const auto& rule : expected_chain_flushes) {
    EXPECT_CALL(*datapath_, FlushChain(rule.ip_family, Iptables::Table::kMangle,
                                       rule.chain));
  }
  for (const auto& rule : expected_chain_deletions) {
    EXPECT_CALL(*datapath_, RemoveChain(rule.ip_family,
                                        Iptables::Table::kMangle, rule.chain));
  }

  multicast_counters_svc_->Stop();
}

TEST_F(MulticastCountersServiceTest, OnPhysicalEthernetDeviceAdded) {
  // The following commands are expected when an ethernet device comes up.
  // Jump rules are added if they did not exist before.
  ShillClient::Device eth0_device =
      MakeShillDevice(ShillClient::Device::Type::kEthernet, "eth0", 1);
  std::vector<std::string> mdns_args = {"-i", "eth0", "-j", "rx_ethernet_mdns"};
  EXPECT_CALL(*datapath_,
              ModifyIptables(IpFamily::kDual, Iptables::Table::kMangle,
                             Iptables::Command::kC, _, _, _))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(*datapath_,
              ModifyIptables(IpFamily::kDual, Iptables::Table::kMangle,
                             Iptables::Command::kA, StrEq("rx_mdns"),
                             ElementsAreArray(mdns_args), _))
      .WillOnce(Return(true));
  std::vector<std::string> ssdp_args = {"-i", "eth0", "-j", "rx_ethernet_ssdp"};
  EXPECT_CALL(*datapath_,
              ModifyIptables(IpFamily::kDual, Iptables::Table::kMangle,
                             Iptables::Command::kC, StrEq("rx_ssdp"),
                             ElementsAreArray(ssdp_args), _))
      .WillOnce(Return(true));
  multicast_counters_svc_->OnPhysicalDeviceAdded(eth0_device);
}

TEST_F(MulticastCountersServiceTest, OnPhysicalWifiDeviceAdded) {
  // The following commands are expected when a wifi device comes up.
  // Jump rules are added if they did not exist before.
  ShillClient::Device wlan0_device =
      MakeShillDevice(ShillClient::Device::Type::kWifi, "wlan0", 3);
  std::vector<std::string> mdns_args = {"-i", "wlan0", "-j", "rx_wifi_mdns"};
  EXPECT_CALL(*datapath_,
              ModifyIptables(IpFamily::kDual, Iptables::Table::kMangle,
                             Iptables::Command::kC, _, _, _))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(*datapath_,
              ModifyIptables(IpFamily::kDual, Iptables::Table::kMangle,
                             Iptables::Command::kA, StrEq("rx_mdns"),
                             ElementsAreArray(mdns_args), _))
      .WillOnce(Return(true));
  std::vector<std::string> ssdp_args = {"-i", "wlan0", "-j", "rx_wifi_ssdp"};
  EXPECT_CALL(*datapath_,
              ModifyIptables(IpFamily::kDual, Iptables::Table::kMangle,
                             Iptables::Command::kA, StrEq("rx_ssdp"),
                             ElementsAreArray(ssdp_args), _))
      .WillOnce(Return(true));
  multicast_counters_svc_->OnPhysicalDeviceAdded(wlan0_device);
}

TEST_F(MulticastCountersServiceTest, OnPhysicalWifiDeviceAlreadyAdded) {
  // If jump rule for a specific interface is already added, skip adding jump
  // rule.
  ShillClient::Device wlan0_device =
      MakeShillDevice(ShillClient::Device::Type::kWifi, "wlan0", 3);
  std::vector<std::string> mdns_args = {"-i", "wlan0", "-j", "rx_wifi_mdns"};
  EXPECT_CALL(*datapath_,
              ModifyIptables(IpFamily::kDual, Iptables::Table::kMangle,
                             Iptables::Command::kC, _, _, _))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*datapath_,
              ModifyIptables(IpFamily::kDual, Iptables::Table::kMangle,
                             Iptables::Command::kA, StrEq("rx_mdns"),
                             ElementsAreArray(mdns_args), _))
      .Times(0);
  multicast_counters_svc_->OnPhysicalDeviceAdded(wlan0_device);
}

TEST_F(MulticastCountersServiceTest, OnPhysicalCellDeviceAdded) {
  // Modification to iptables are not expected when a cell device comes up.
  ShillClient::Device cell_device =
      MakeShillDevice(ShillClient::Device::Type::kCellular, "wwan0", 3);
  cell_device.primary_multiplexed_interface = "wwan0";
  EXPECT_CALL(*datapath_,
              ModifyIptables(IpFamily::kDual, Iptables::Table::kMangle,
                             Iptables::Command::kC, _, _, _))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(*datapath_,
              ModifyIptables(_, Iptables::Table::kMangle, _, _, _, _))
      .Times(0);
  multicast_counters_svc_->OnPhysicalDeviceAdded(cell_device);
}

TEST_F(MulticastCountersServiceTest, GetCounters) {
  EXPECT_CALL(*datapath_,
              DumpIptables(IpFamily::kIPv4, Iptables::Table::kMangle))
      .WillOnce(Return(kIptablesOutput));
  EXPECT_CALL(*datapath_,
              DumpIptables(IpFamily::kIPv6, Iptables::Table::kMangle))
      .WillOnce(Return(kIp6tablesOutput));
  auto actual = multicast_counters_svc_->GetCounters();

  std::map<CounterKey, uint64_t> expected{
      {{MulticastCountersService::MulticastProtocolType::kMdns,
        MulticastCountersService::MulticastTechnologyType::kEthernet},
       9867U},
      {{MulticastCountersService::MulticastProtocolType::kMdns,
        MulticastCountersService::MulticastTechnologyType::kWifi},
       500U},
      {{MulticastCountersService::MulticastProtocolType::kSsdp,
        MulticastCountersService::MulticastTechnologyType::kEthernet},
       150U},
      {{MulticastCountersService::MulticastProtocolType::kSsdp,
        MulticastCountersService::MulticastTechnologyType::kWifi},
       50U},
  };

  EXPECT_TRUE(CompareCounters(expected, actual.value()));
}

TEST_F(MulticastCountersServiceTest, GetCountersEmptyIptablesOutput) {
  EXPECT_CALL(*datapath_,
              DumpIptables(IpFamily::kIPv4, Iptables::Table::kMangle))
      .WillOnce(Return(""));
  EXPECT_CALL(*datapath_,
              DumpIptables(IpFamily::kIPv6, Iptables::Table::kMangle))
      .Times(0);
  auto actual = multicast_counters_svc_->GetCounters();
  EXPECT_FALSE(actual.has_value());
}

TEST_F(MulticastCountersServiceTest, GetCountersNoRelatedChain) {
  const char kIptablesOutputUnrelated[] = R"(
    Chain PREROUTING (policy ACCEPT 8949 packets, 872859 bytes)
        pkts      bytes target     prot opt in     out     source               destination         
        9109   892835 CONNMARK   all  --  eth0   *       0.0.0.0/0            0.0.0.0/0            CONNMARK restore mask 0x3f00
          0        0 CONNMARK   all  --  wlan0  *       0.0.0.0/0            0.0.0.0/0            CONNMARK restore mask 0x3f00
  )";
  const char kIp6tablesOutputUnrelated[] = R"(
    Chain PREROUTING (policy ACCEPT 98844 packets, 15455949 bytes)
        pkts      bytes target     prot opt in     out     source               destination         
      99159 15483417 CONNMARK   all      eth0   *       ::/0                 ::/0                 CONNMARK restore mask 0x3f00
          0        0 CONNMARK   all      wlan0  *       ::/0                 ::/0                 CONNMARK restore mask 0x3f00
  )";
  EXPECT_CALL(*datapath_,
              DumpIptables(IpFamily::kIPv4, Iptables::Table::kMangle))
      .WillOnce(Return(kIptablesOutputUnrelated));
  EXPECT_CALL(*datapath_,
              DumpIptables(IpFamily::kIPv6, Iptables::Table::kMangle))
      .WillOnce(Return(kIp6tablesOutputUnrelated));
  auto actual = multicast_counters_svc_->GetCounters();
  EXPECT_TRUE(IsEmptyCounters(actual.value()));
}

TEST_F(MulticastCountersServiceTest, GetCountersOnlyChainAndHeader) {
  const char kIptablesOutputUnrelated[] = R"(
    Chain PREROUTING (policy ACCEPT 8949 packets, 872859 bytes)
        pkts      bytes target     prot opt in     out     source               destination         
  )";
  const char kIp6tablesOutputUnrelated[] = R"(
    Chain PREROUTING (policy ACCEPT 98844 packets, 15455949 bytes)
        pkts      bytes target     prot opt in     out     source               destination         
  )";
  EXPECT_CALL(*datapath_,
              DumpIptables(IpFamily::kIPv4, Iptables::Table::kMangle))
      .WillOnce(Return(kIptablesOutputUnrelated));
  EXPECT_CALL(*datapath_,
              DumpIptables(IpFamily::kIPv6, Iptables::Table::kMangle))
      .WillOnce(Return(kIp6tablesOutputUnrelated));
  auto actual = multicast_counters_svc_->GetCounters();
  EXPECT_TRUE(IsEmptyCounters(actual.value()));
}

}  // namespace
}  // namespace patchpanel
