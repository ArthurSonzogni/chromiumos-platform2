// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/mock_datapath.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace patchpanel {

using ::testing::Return;

MockDatapath::MockDatapath(MinijailedProcessRunner* process_runner,
                           System* system)
    : Datapath(process_runner, nullptr, system) {
  // Make the functions returning true for success by default, to trigger the
  // normal path in the production code and also reduce the error output for
  // easier actionability of the test output.
  ON_CALL(*this, NetnsAttachName).WillByDefault(Return(true));
  ON_CALL(*this, NetnsDeleteName).WillByDefault(Return(true));
  ON_CALL(*this, AddBridge).WillByDefault(Return(true));
  ON_CALL(*this, AddToBridge).WillByDefault(Return(true));
  ON_CALL(*this, ConnectVethPair).WillByDefault(Return(true));
  ON_CALL(*this, MaskInterfaceFlags).WillByDefault(Return(true));
  ON_CALL(*this, AddIPv4RouteToTable).WillByDefault(Return(true));
  ON_CALL(*this, AddIPv4Route).WillByDefault(Return(true));
  ON_CALL(*this, SetConntrackHelpers).WillByDefault(Return(true));
  ON_CALL(*this, SetRouteLocalnet).WillByDefault(Return(true));
  ON_CALL(*this, ModprobeAll).WillByDefault(Return(true));
  ON_CALL(*this, AddChain).WillByDefault(Return(true));
  ON_CALL(*this, RemoveChain).WillByDefault(Return(true));
  ON_CALL(*this, FlushChain).WillByDefault(Return(true));
  ON_CALL(*this, ModifyChain).WillByDefault(Return(true));
  ON_CALL(*this, ModifyClatAcceptRules).WillByDefault(Return(true));
  ON_CALL(*this, ModifyIptables).WillByDefault(Return(true));
  ON_CALL(*this, AddIPv6NeighborProxy).WillByDefault(Return(true));
  ON_CALL(*this, AddIPv6HostRoute).WillByDefault(Return(true));
  ON_CALL(*this, StartDownstreamNetwork).WillByDefault(Return(true));
}

MockDatapath::~MockDatapath() = default;

}  // namespace patchpanel
