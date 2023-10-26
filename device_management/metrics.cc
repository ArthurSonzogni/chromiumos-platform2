// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device_management/metrics.h"

namespace device_management {

namespace {

// Record the InstallAttributesStatus when device_management initializes.
constexpr char kInstallAttributesStatus[] =
    "Platform.DeviceManagement.InstallAttributesStatus";

}  // namespace

void Metrics::ReportInstallAttributesStatus(InstallAttributes::Status status) {
  metrics_library_->SendEnumToUMA(kInstallAttributesStatus, status);
}

}  // namespace device_management
