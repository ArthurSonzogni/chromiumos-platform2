// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/clat_service.h"

#include <base/logging.h>

#include "patchpanel/shill_client.h"

namespace patchpanel {

ClatService::ClatService() = default;
ClatService::~ClatService() = default;

// TODO(b/278970851): Do the actual implementation
void ClatService::OnShillDefaultLogicalDeviceChanged(
    const ShillClient::Device* new_device,
    const ShillClient::Device* prev_device) {
  return;
}

// TODO(b/278970851): Do the actual implementation
void ClatService::OnDefaultLogicalDeviceIPConfigChanged(
    const ShillClient::Device& default_logical_device) {
  return;
}

}  // namespace patchpanel
