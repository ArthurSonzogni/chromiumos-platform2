// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHPANEL_NETWORK_MOCK_ROUTING_TABLE_H_
#define PATCHPANEL_NETWORK_MOCK_ROUTING_TABLE_H_

#include <gmock/gmock.h>

#include "patchpanel/network/routing_table.h"

namespace patchpanel {

class MockRoutingTable : public RoutingTable {
 public:
  MockRoutingTable();
  MockRoutingTable(const MockRoutingTable&) = delete;
  MockRoutingTable& operator=(const MockRoutingTable&) = delete;

  ~MockRoutingTable() override;

  MOCK_METHOD(void, Start, (), (override));
  MOCK_METHOD(bool, AddRoute, (int, const RoutingTableEntry&), (override));
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

}  // namespace patchpanel

#endif  // PATCHPANEL_NETWORK_MOCK_ROUTING_TABLE_H_
