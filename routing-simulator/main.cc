// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "routing-simulator/packet.h"
#include "routing-simulator/process_executor.h"
#include "routing-simulator/route_manager.h"

#include <iostream>

#include <base/logging.h>

int main() {
  while (true) {
    const auto process_executor_ptr =
        routing_simulator::ProcessExecutor::Create();
    routing_simulator::RouteManager route_manager(process_executor_ptr.get());
    route_manager.BuildTables();
    auto packet =
        routing_simulator::Packet::CreatePacketFromStdin(std::cin, std::cout);
    const auto result = route_manager.ProcessPacketWithMutation(packet);
    result.Output(std::cout);
  }
}
