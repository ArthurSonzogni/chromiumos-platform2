// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "routing-simulator/packet.h"

#include <iostream>

#include <base/logging.h>

int main() {
  // TODO(b/307460180): Add implementations later.
  LOG(INFO) << "hello";
  routing_simulator::Packet::CreatePacketFromStdin(std::cin, std::cout);
  return 0;
}
