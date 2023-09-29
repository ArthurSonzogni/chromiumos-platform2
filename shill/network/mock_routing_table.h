// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_NETWORK_MOCK_ROUTING_TABLE_H_
#define SHILL_NETWORK_MOCK_ROUTING_TABLE_H_

#include <gmock/gmock.h>

#include "shill/network/routing_table.h"

namespace shill {

class MockRoutingTable : public RoutingTable {
 public:
  MockRoutingTable();
  MockRoutingTable(const MockRoutingTable&) = delete;
  MockRoutingTable& operator=(const MockRoutingTable&) = delete;

  ~MockRoutingTable() override;

  MOCK_METHOD(void, Start, (), (override));
  MOCK_METHOD(void, Stop, (), (override));
  MOCK_METHOD(bool, AddRoute, (int, const RoutingTableEntry&), (override));
  MOCK_METHOD(bool,
              GetDefaultRoute,
              (int, net_base::IPFamily, RoutingTableEntry*),
              (override));
  MOCK_METHOD(bool,
              SetDefaultRoute,
              (int, const net_base::IPAddress&, uint32_t),
              (override));
  MOCK_METHOD(bool,
              CreateBlackholeRoute,
              (int, net_base::IPFamily, uint32_t, uint32_t),
              (override));
  MOCK_METHOD(void, FlushRoutes, (int), (override));
  MOCK_METHOD(void, FlushRoutesWithTag, (int, net_base::IPFamily), (override));
  MOCK_METHOD(void, ResetTable, (int), (override));
};

}  // namespace shill

#endif  // SHILL_NETWORK_MOCK_ROUTING_TABLE_H_
