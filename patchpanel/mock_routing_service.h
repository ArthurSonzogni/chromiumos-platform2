// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHPANEL_MOCK_ROUTING_SERVICE_H_
#define PATCHPANEL_MOCK_ROUTING_SERVICE_H_

#include "patchpanel/routing_service.h"

#include <string_view>
#include <optional>

#include <base/files/scoped_file.h>
#include <gmock/gmock.h>

namespace patchpanel {

class MockRoutingService : public RoutingService {
 public:
  MockRoutingService();
  MockRoutingService(const MockRoutingService&) = delete;
  MockRoutingService& operator=(const MockRoutingService&) = delete;
  virtual ~MockRoutingService();

  MOCK_METHOD(int, AllocateNetworkID, (), (override));
  MOCK_METHOD(bool,
              AssignInterfaceToNetwork,
              (int, std::string_view, base::ScopedFD),
              (override));
  MOCK_METHOD(void, ForgetNetworkID, (int), (override));
  MOCK_METHOD(bool,
              TagSocket,
              (int,
               std::optional<int>,
               VPNRoutingPolicy,
               std::optional<TrafficAnnotationId>),
              (override));
};

}  // namespace patchpanel

#endif  // PATCHPANEL_MOCK_ROUTING_SERVICE_H_
