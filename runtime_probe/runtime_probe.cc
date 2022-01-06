// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <iostream>

#include <base/at_exit.h>
#include <base/command_line.h>
#include <base/json/json_reader.h>
#include <base/logging.h>
#include <brillo/flag_helper.h>
#include <brillo/syslog_logging.h>

#include "runtime_probe/daemon.h"
#include "runtime_probe/probe_config.h"
#include "runtime_probe/probe_config_loader_impl.h"
#include "runtime_probe/probe_function.h"
#include "runtime_probe/system/context_factory_impl.h"
#include "runtime_probe/system/context_helper_impl.h"
#include "runtime_probe/system/context_runtime_impl.h"
#include "runtime_probe/system_property_impl.h"

namespace {
enum ExitStatus {
  kSuccess = EXIT_SUCCESS,  // 0
  kUnknownError = 1,
  kFailedToParseProbeStatementFromArg = 2,
  kArgumentError = 3,
  kFailedToLoadProbeConfig = 11,
  kFailToParseProbeArgFromConfig = 12,
};

void SetVerbosityLevel(uint32_t verbosity_level) {
  verbosity_level = std::min(verbosity_level, 3u);
  // VLOG uses negative log level.
  logging::SetMinLogLevel(-(static_cast<int32_t>(verbosity_level)));
}

int RunAsHelper() {
  const auto* command_line = base::CommandLine::ForCurrentProcess();
  const auto args = command_line->GetArgs();

  for (size_t i = 0; i < args.size(); ++i) {
    DVLOG(1) << "Got arguments, index " << i << " = " << args[i];
  }

  if (args.size() != 1) {
    LOG(ERROR) << "Helper only consumes a single probe statement";
    return kFailedToParseProbeStatementFromArg;
  }

  auto val = base::JSONReader::Read(args[0]);
  if (!val || !val->is_dict()) {
    LOG(ERROR) << "Failed to parse the probe statement to JSON";
    return kFailedToParseProbeStatementFromArg;
  }

  runtime_probe::ContextHelperImpl context;

  auto probe_function = runtime_probe::ProbeFunction::FromValue(*val);
  if (probe_function == nullptr) {
    LOG(ERROR) << "Failed to convert a probe statement to probe function";
    return kFailedToParseProbeStatementFromArg;
  }

  std::string output;
  int ret = probe_function->EvalInHelper(&output);
  if (ret)
    return ret;

  std::cout << output << std::flush;
  return ExitStatus::kSuccess;
}

int RunAsDaemon() {
  if constexpr (USE_FACTORY_RUNTIME_PROBE) {
    LOG(FATAL) << "Unexpected error.  Daemon mode should never be reachable "
                  "in factory_runtime_probe.";
    return ExitStatus::kUnknownError;
  }

  LOG(INFO) << "Starting Runtime Probe. Running in daemon mode";
  runtime_probe::ContextRuntimeImpl context;
  runtime_probe::Daemon daemon;
  return daemon.Run();
}

// Invoke as a command line tool. Device can load arbitrary probe config
// iff cros_debug == 1
int RunningInCli(const std::string& config_file_path, bool to_stdout) {
  LOG(INFO) << "Starting Runtime Probe. Running in CLI mode";

#if USE_FACTORY_RUNTIME_PROBE
  runtime_probe::ContextFactoryImpl context;
#else
  runtime_probe::ContextRuntimeImpl context;
#endif

  const auto probe_config_loader =
      std::make_unique<runtime_probe::ProbeConfigLoaderImpl>();

  base::Optional<runtime_probe::ProbeConfigData> probe_config_data;
  if (config_file_path == "") {
    probe_config_data = probe_config_loader->LoadDefault();
  } else {
    probe_config_data =
        probe_config_loader->LoadFromFile(base::FilePath{config_file_path});
  }
  if (!probe_config_data) {
    LOG(ERROR) << "Failed to load probe config";
    return ExitStatus::kFailedToLoadProbeConfig;
  }

  LOG(INFO) << "Load probe config from: " << probe_config_data->path
            << " (checksum: " << probe_config_data->sha1_hash << ")";

  auto probe_config =
      runtime_probe::ProbeConfig::FromValue(probe_config_data->config);
  if (!probe_config) {
    LOG(ERROR) << "Failed to parse from argument from ProbeConfig";
    return ExitStatus::kFailToParseProbeArgFromConfig;
  }

  const auto probe_result = probe_config->Eval();
  if (to_stdout) {
    LOG(INFO) << "Dumping probe results to stdout";
    std::cout << probe_result;
  } else {
    LOG(INFO) << probe_result;
  }

  return ExitStatus::kSuccess;
}

}  // namespace

int main(int argc, char* argv[]) {
  if constexpr (USE_FACTORY_RUNTIME_PROBE) {
    int cros_debug;
    if (!runtime_probe::SystemPropertyImpl().GetInt("cros_debug",
                                                    &cros_debug) ||
        cros_debug != 1) {
      LOG(FATAL) << "factory_runtime_probe should never run in normal mode.";
      return ExitStatus::kUnknownError;
    }
  }

  brillo::InitLog(brillo::kLogToSyslog | brillo::kLogToStderrIfTty);

  // Flags are subject to change
  DEFINE_string(config_file_path, "",
                "File path to probe config, empty to use default one");

#if !USE_FACTORY_RUNTIME_PROBE
  DEFINE_bool(dbus, false, "Run in the mode to respond D-Bus call");
#else
  constexpr bool FLAGS_dbus = false;  // DBus daemon mode is not available in
                                      // factory_runtime_probe.
#endif

  DEFINE_bool(helper, false, "Run in the mode to execute probe function");
  DEFINE_bool(to_stdout, false, "Output probe result to stdout");
  DEFINE_uint32(verbosity_level, 0,
                "Set verbosity level. Allowed value: 0 to 3");
  brillo::FlagHelper::Init(argc, argv, "ChromeOS runtime probe tool");

  SetVerbosityLevel(FLAGS_verbosity_level);

  if (FLAGS_helper && FLAGS_dbus) {
    LOG(ERROR) << "--helper conflicts with --dbus";
    return ExitStatus::kArgumentError;
  }
  if ((FLAGS_helper || FLAGS_dbus) &&
      (FLAGS_to_stdout || FLAGS_config_file_path != "")) {
    LOG(WARNING) << "--to_stdout and --config_file_path are not supported in "
                    "helper mode and dbus mode.";
  }

  if (FLAGS_helper)
    return RunAsHelper();
  if (FLAGS_dbus)
    return RunAsDaemon();

  // Required by dbus in libchrome.
  base::AtExitManager at_exit_manager;
  return RunningInCli(FLAGS_config_file_path, FLAGS_to_stdout);
}
