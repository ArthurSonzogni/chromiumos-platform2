// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/downstream_network_service.h"

namespace patchpanel {

CreateDownstreamNetworkResult DownstreamNetworkResultToUMAEvent(
    patchpanel::DownstreamNetworkResult result) {
  switch (result) {
    case patchpanel::DownstreamNetworkResult::SUCCESS:
      return CreateDownstreamNetworkResult::kSuccess;
    case patchpanel::DownstreamNetworkResult::INVALID_ARGUMENT:
      return CreateDownstreamNetworkResult::kInvalidArgument;
    case patchpanel::DownstreamNetworkResult::INTERFACE_USED:
      return CreateDownstreamNetworkResult::kDownstreamUsed;
    case patchpanel::DownstreamNetworkResult::ERROR:
      return CreateDownstreamNetworkResult::kInternalError;
    case patchpanel::DownstreamNetworkResult::DHCP_SERVER_FAILURE:
      return CreateDownstreamNetworkResult::kDHCPServerFailure;
    case patchpanel::DownstreamNetworkResult::UPSTREAM_UNKNOWN:
      return CreateDownstreamNetworkResult::kUpstreamUnknown;
    case patchpanel::DownstreamNetworkResult::DATAPATH_ERROR:
      return CreateDownstreamNetworkResult::kDatapathError;
    case patchpanel::DownstreamNetworkResult::INVALID_REQUEST:
      return CreateDownstreamNetworkResult::kInvalidRequest;
    default:
      return CreateDownstreamNetworkResult::kUnknown;
  }
}

}  // namespace patchpanel
