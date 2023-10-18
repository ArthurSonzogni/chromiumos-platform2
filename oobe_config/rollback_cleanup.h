// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef OOBE_CONFIG_ROLLBACK_CLEANUP_H_
#define OOBE_CONFIG_ROLLBACK_CLEANUP_H_

namespace hwsec {
class FactoryImpl;
}

namespace oobe_config {

class FileHandler;
class EnterpriseRollbackMetricsHandler;

// Checks if oobe is completed or not and does cleanup steps for depending
// on that.
void RollbackCleanup(
    const oobe_config::FileHandler* file_handler,
    const oobe_config::EnterpriseRollbackMetricsHandler* metrics_handler,
    hwsec::FactoryImpl* hwsec_factory);

}  // namespace oobe_config

#endif  // OOBE_CONFIG_ROLLBACK_CLEANUP_H_
