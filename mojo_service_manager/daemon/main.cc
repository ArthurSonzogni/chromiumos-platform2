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

#include "mojo_service_manager/daemon/configuration.h"
#include "mojo_service_manager/daemon/daemon.h"
#include "mojo_service_manager/daemon/service_policy_loader.h"

namespace {

namespace service_manager = chromeos::mojo_service_manager;

// The path of the service manager's socket server.
constexpr char kSocketPath[] = "/run/mojo/service_manager";

// The policy directory path.
constexpr char kPolicyDirectoryPath[] = "/etc/mojo/service_manager/policy";

// The extra policy directory path which is only used in dev mode.
constexpr char kExtraPolicyDirectoryPathInDevMode[] =
    "/usr/local/etc/mojo/service_manager/policy";

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
  DEFINE_bool(permissive, false,
              "Indicates whether the service manager daemon is in the "
              "permissive mode. In permissive mode, the requests with wrong "
              "identity won't be rejected.");

  brillo::FlagHelper::Init(argc, argv, "ChromeOS mojo service manager.");

  brillo::InitLog(brillo::kLogToStderr | brillo::kLogToSyslog);
  logging::SetMinLogLevel(FLAGS_log_level);

  mojo::core::Init(mojo::core::Configuration{.is_broker_process = true});

  service_manager::ServicePolicyMap policy_map;
  service_manager::LoadAllServicePolicyFileFromDirectory(
      base::FilePath{kPolicyDirectoryPath}, &policy_map);
  if (IsDevMode()) {
    LOG(INFO) << "DevMode is enabled, load extra configs from "
              << kExtraPolicyDirectoryPathInDevMode;
    service_manager::LoadAllServicePolicyFileFromDirectory(
        base::FilePath{kExtraPolicyDirectoryPathInDevMode}, &policy_map);
  }

  service_manager::Configuration configuration{};
  if (FLAGS_permissive) {
    configuration.is_permissive = true;
  }

  service_manager::Daemon daemon(base::FilePath{kSocketPath},
                                 std::move(configuration),
                                 std::move(policy_map));
  return daemon.Run();
}
