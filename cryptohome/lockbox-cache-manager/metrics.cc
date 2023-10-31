// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/lockbox-cache-manager/metrics.h"

namespace cryptohome {

namespace {

// Record the Install-attributes migration status.
constexpr char kInstallAttributesMigrationStatus[] =
    "Platform.Cryptohome.InstallAttributesMigrationStatus";

}  // namespace

void Metrics::ReportInstallAttributesMigrationStatus(MigrationStatus status) {
  metrics_library_->SendEnumToUMA(kInstallAttributesMigrationStatus, status);
}

}  // namespace cryptohome
