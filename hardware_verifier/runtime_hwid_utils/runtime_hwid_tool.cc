// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <brillo/flag_helper.h>
#include <brillo/syslog_logging.h>
#include <hardware_verifier/runtime_hwid_utils/runtime_hwid_utils_impl.h>

namespace {

constexpr char kInfoText[] =
    "ChromeOS Runtime HWID Tool.\n\n"
    "This tool is used to manage the Runtime HWID on the device.\n\n"
    "Available Commands:\n"
    "  get   - Gets the Runtime HWID.\n";
constexpr char kGetAction[] = "get";

}  // namespace

int main(int argc, char* argv[]) {
  brillo::InitLog(brillo::kLogToStderr);

  DEFINE_int32(verbosity, 0,
               "Verbosity level, range from 0 to 5.  The greater number is "
               "set, the more detail messages will be printed.");

  brillo::FlagHelper::Init(argc, argv, kInfoText);

  logging::SetMinLogLevel(-FLAGS_verbosity);

  const auto* cl = base::CommandLine::ForCurrentProcess();
  const auto args = cl->GetArgs();
  if (args.size() != 1) {
    LOG(ERROR) << "Invalid number of command line arguments. Use --help for "
                  "the usage.";
    return EXIT_FAILURE;
  }
  if (args[0] != kGetAction) {
    LOG(ERROR) << "Unknown command line arguments. Use --help for the usage.";
    return EXIT_FAILURE;
  }

  hardware_verifier::RuntimeHWIDUtilsImpl runtime_hwid_utils;
  const auto runtime_hwid = runtime_hwid_utils.GetRuntimeHWID();
  if (!runtime_hwid.has_value()) {
    LOG(ERROR) << "Failed to get Runtime HWID.";
    return EXIT_FAILURE;
  }

  std::cout << *runtime_hwid << std::endl;

  return EXIT_SUCCESS;
}
