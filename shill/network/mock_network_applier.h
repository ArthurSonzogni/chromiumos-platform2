// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_NETWORK_MOCK_NETWORK_APPLIER_H_
#define SHILL_NETWORK_MOCK_NETWORK_APPLIER_H_

#include <gmock/gmock.h>

#include "shill/network/network_applier.h"

namespace shill {

class MockNetworkApplier : public NetworkApplier {
 public:
  MockNetworkApplier();
  MockNetworkApplier(const MockNetworkApplier&) = delete;
  MockNetworkApplier& operator=(const MockNetworkApplier&) = delete;
  ~MockNetworkApplier();

  MOCK_METHOD(void, ApplyMTU, (int, int), (override));
};

}  // namespace shill

#endif  // SHILL_NETWORK_MOCK_NETWORK_APPLIER_H_
