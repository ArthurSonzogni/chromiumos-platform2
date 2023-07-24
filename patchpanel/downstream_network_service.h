// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHPANEL_DOWNSTREAM_NETWORK_SERVICE_H_
#define PATCHPANEL_DOWNSTREAM_NETWORK_SERVICE_H_

#include <patchpanel/proto_bindings/patchpanel_service.pb.h>

#include "patchpanel/metrics.h"

namespace patchpanel {

CreateDownstreamNetworkResult DownstreamNetworkResultToUMAEvent(
    patchpanel::DownstreamNetworkResult result);

}  // namespace patchpanel

#endif  // PATCHPANEL_DOWNSTREAM_NETWORK_SERVICE_H_
