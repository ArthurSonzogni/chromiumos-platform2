// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHPANEL_MOCK_MULTICAST_COUNTERS_SERVICE_H_
#define PATCHPANEL_MOCK_MULTICAST_COUNTERS_SERVICE_H_

#include <map>
#include <optional>
#include <string>
#include <string_view>

#include <gmock/gmock.h>

#include "patchpanel/multicast_counters_service.h"
#include "patchpanel/shill_client.h"

namespace patchpanel {

class MockMulticastCountersService : public MulticastCountersService {
 public:
  MockMulticastCountersService();
  ~MockMulticastCountersService();

  MOCK_METHOD(void, Start, (), (override));
  MOCK_METHOD(void, Stop, (), (override));
  MOCK_METHOD(void,
              OnPhysicalDeviceAdded,
              (const ShillClient::Device& device),
              (override));
  MOCK_METHOD(void,
              OnPhysicalDeviceRemoved,
              (const ShillClient::Device& device),
              (override));
  MOCK_METHOD((std::optional<std::map<CounterKey, uint64_t>>),
              GetCounters,
              (),
              (override));
  MOCK_METHOD(void,
              SetupJumpRules,
              (Iptables::Command command,
               std::string_view ifname,
               std::string_view technology),
              (override));
  MOCK_METHOD(bool,
              ParseIptableOutput,
              (std::string_view output,
               (std::map<CounterKey, uint64_t> * counter)),
              (override));
};

}  // namespace patchpanel

#endif  // PATCHPANEL_MOCK_MULTICAST_COUNTERS_SERVICE_H_
