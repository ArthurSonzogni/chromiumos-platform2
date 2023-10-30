// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "routing-simulator/packet.h"

namespace routing_simulator {

// static
// TODO(b/307460180): Implement below.
std::optional<Packet> CreatePacketFromStdin() {
  // prompt users to input each field
  return std::nullopt;
}

Packet::Packet(const Packet& other) = default;
Packet& Packet::operator=(const Packet& other) = default;

bool Packet::operator==(const Packet& rhs) const = default;

Packet::Packet(net_base::IPFamily ip_family)
    : source_ip_(net_base::IPAddress(ip_family)),
      destination_ip_(net_base::IPAddress(ip_family)) {}

}  // namespace routing_simulator
