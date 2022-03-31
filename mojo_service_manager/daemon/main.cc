// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>

#include <base/files/file_path.h>
#include <base/logging.h>
#include <brillo/flag_helper.h>
#include <brillo/syslog_logging.h>
#include <mojo/core/embedder/embedder.h>
#include <vboot/crossystem.h>

#include "mojo_service_manager/daemon/constants.h"
#include "mojo_service_manager/daemon/daemon.h"
#include "mojo_service_manager/daemon/service_policy_loader.h"

namespace {

namespace service_manager = chromeos::mojo_service_manager;

bool IsDevMode() {
  int value = ::VbGetSystemPropertyInt("cros_debug");
  LOG_IF(ERROR, value == -1) << "Cannot get cros_debug from crossystem.";
  // If fails to get value, the value will be -1. Treat it as false.
  return value == 1;
}

}  // namespace

int main(int argc, char* argv[]) {
  // Flags are subject to change
  DEFINE_int32(log_level, 0,
               "Logging level - 0: LOG(INFO), 1: LOG(WARNING), 2: LOG(ERROR), "
               "-1: VLOG(1), -2: VLOG(2), ...");

  brillo::FlagHelper::Init(argc, argv, "ChromeOS mojo service manager.");

  brillo::InitLog(brillo::kLogToStderr | brillo::kLogToSyslog);
  logging::SetMinLogLevel(FLAGS_log_level);

  mojo::core::Init(mojo::core::Configuration{.is_broker_process = true});

  service_manager::ServicePolicyMap policy_map;
  service_manager::LoadAllServicePolicyFileFromDirectory(
      base::FilePath{service_manager::kPolicyDirectoryPath}, &policy_map);
  if (IsDevMode()) {
    LOG(INFO) << "DevMode is enabled, load extra configs from "
              << service_manager::kExtraPolicyDirectoryPathInDevMode;
    service_manager::LoadAllServicePolicyFileFromDirectory(
        base::FilePath{service_manager::kExtraPolicyDirectoryPathInDevMode},
        &policy_map);
  }
  service_manager::Daemon daemon(base::FilePath{service_manager::kSocketPath},
                                 std::move(policy_map));
  return daemon.Run();
}
