// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Command-line utility to access to the Chrome OS model configuration.

#include <iostream>
#include <string>

#include <base/check_op.h>
#include <base/command_line.h>
#include <base/files/file_path.h>
#include <base/logging.h>
#include <brillo/flag_helper.h>

#include "chromeos-config/libcros_config/cros_config.h"

int main(int argc, char* argv[]) {
  std::string usage = "Chrome OS Model Configuration\n\nUsage:\n  " +
                      std::string(argv[0]) + " [flags] <path> <key>\n\n" +
                      "Set CROS_CONFIG_DEBUG=1 in your environment to emit " +
                      "debug logging messages.\n";
  brillo::FlagHelper::Init(argc, argv, usage);

  logging::LoggingSettings settings;
  settings.logging_dest = logging::LOG_TO_FILE;
  settings.log_file_path = "/var/log/cros_config.log";
  settings.lock_log = logging::DONT_LOCK_LOG_FILE;
  settings.delete_old = logging::APPEND_TO_OLD_LOG_FILE;
  logging::InitLogging(settings);
  logging::SetMinLogLevel(-3);

  brillo::CrosConfig cros_config;
  if (!cros_config.Init()) {
    return 1;
  }

  base::CommandLine::StringVector args =
      base::CommandLine::ForCurrentProcess()->GetArgs();

  if (args.size() != 2) {
    std::cerr << usage << "\nPass --help for more information." << std::endl;
    return 1;
  }

  std::string path = args[0];
  std::string property = args[1];

  std::string value;
  bool result = cros_config.GetString(path, property, &value);
  if (!result) {
    return 1;
  }

  std::cout << value;
  return 0;
}
