// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHPANEL_MOCK_COUNTERS_SERVICE_H_
#define PATCHPANEL_MOCK_COUNTERS_SERVICE_H_

#include <string>

#include <gmock/gmock.h>
#include <patchpanel/proto_bindings/patchpanel_service.pb.h>

#include "patchpanel/conntrack_monitor.h"
#include "patchpanel/counters_service.h"
#include "patchpanel/datapath.h"

namespace patchpanel {

class MockCountersService : public CountersService {
 public:
  MockCountersService(Datapath* datapath, ConntrackMonitor* monitor);
  explicit MockCountersService(const MockCountersService&) = delete;
  MockCountersService& operator=(const MockCountersService&) = delete;
  virtual ~MockCountersService();

  MOCK_METHOD(void,
              OnPhysicalDeviceAdded,
              (const std::string& ifname),
              (override));
  MOCK_METHOD(void,
              OnPhysicalDeviceRemoved,
              (const std::string& ifname),
              (override));
};

}  // namespace patchpanel

#endif  // PATCHPANEL_MOCK_COUNTERS_SERVICE_H_
