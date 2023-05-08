// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHPANEL_PROTO_UTILS_H_
#define PATCHPANEL_PROTO_UTILS_H_

#include <map>
#include <string>

#include <patchpanel/proto_bindings/patchpanel_service.pb.h>

#include "patchpanel/datapath.h"
#include "patchpanel/device.h"
#include "patchpanel/subnet.h"

namespace patchpanel {

// Fills a protobuf NetworkDevice object with the given |virtual_device| Device.
void FillDeviceProto(const Device& virtual_device, NetworkDevice* output);

// Fills a protobuf IPv4Subnet object with the IPv4 address |base_addr| in
// network order and the prefix length |prefix_length|.
void FillSubnetProto(const uint32_t base_addr,
                     int prefix_length,
                     IPv4Subnet* output);
void FillSubnetProto(const Subnet& virtual_subnet, IPv4Subnet* output);

void FillDeviceDnsProxyProto(
    const Device& virtual_device,
    NetworkDevice* output,
    const std::map<std::string, std::string>& ipv4_addrs,
    const std::map<std::string, std::string>& ipv6_addrs);

void FillDownstreamNetworkProto(
    const DownstreamNetworkInfo& downstream_network_info,
    DownstreamNetwork* output);

}  // namespace patchpanel
#endif  // PATCHPANEL_PROTO_UTILS_H_
