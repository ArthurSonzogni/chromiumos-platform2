// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdlib>
#include <optional>
#include <ostream>
#include <string_view>

#include <base/command_line.h>
#include <base/containers/fixed_flat_map.h>
#include <base/logging.h>

#include <patchpanel/dbus/client.h>

namespace {

namespace switches {
constexpr char kFeature[] = "feature";
constexpr char kEnable[] = "enable";
constexpr char kDisable[] = "disable";
constexpr char kHelp[] = "help";

constexpr char kHelpMessage[] =
    "\n"
    "Available Switches: \n"
    "  --feature=wifi,clat\n"
    "    The keyword to identify the feature you want to enable or disable.\n"
    "  --enable\n"
    "    Enable a feature you specify. You can't use this with --disable.\n"
    "  --disable\n"
    "    Disable a feature you specify You can't use this with --enable.\n";
}  // namespace switches

std::optional<patchpanel::Client::FeatureFlag> GetFeatureFlag(
    const std::string_view feature_name) {
  static constexpr auto str2enum =
      base::MakeFixedFlatMap<std::string_view, patchpanel::Client::FeatureFlag>(
          {
              {"wifi-qos", patchpanel::Client::FeatureFlag::kWiFiQoS},
              {"clat", patchpanel::Client::FeatureFlag::kClat},
          });

  const auto it = str2enum.find(feature_name);
  if (it != str2enum.end()) {
    return it->second;
  }
  return std::nullopt;
}

struct RequestBody {
  patchpanel::Client::FeatureFlag flag;
  bool enable;
};

}  // namespace

int main(int argc, char* argv[]) {
  base::CommandLine::Init(argc, argv);
  base::CommandLine* cl = base::CommandLine::ForCurrentProcess();

  if (cl->HasSwitch(switches::kHelp)) {
    LOG(INFO) << switches::kHelpMessage;
    return EXIT_SUCCESS;
  }

  if (cl->GetSwitches().size() != 2) {
    LOG(ERROR) << "Invalid switches; exiting";
    return EXIT_FAILURE;
  }

  if (!cl->HasSwitch(switches::kFeature)) {
    LOG(ERROR) << "You need to specify feature; exiting";
    return EXIT_FAILURE;
  }

  RequestBody request;

  auto flag = GetFeatureFlag(cl->GetSwitchValueASCII(switches::kFeature));
  if (!flag) {
    LOG(ERROR) << "Invalid feature name; exiting";
    return EXIT_FAILURE;
  }
  request.flag = flag.value();

  if (cl->HasSwitch(switches::kEnable)) {
    request.enable = true;
  } else if (cl->HasSwitch(switches::kDisable)) {
    request.enable = false;
  } else {
    LOG(ERROR) << "You need to enter either --enable or --disable; exiting";
    return EXIT_FAILURE;
  }

  auto client = patchpanel::Client::New();
  if (!client) {
    LOG(ERROR) << "Failed to connect to patchpanel client";
    return EXIT_FAILURE;
  }

  bool result = client->SendSetFeatureFlagRequest(request.flag, request.enable);
  if (result) {
    LOG(INFO) << "SUCCESS";
    return EXIT_SUCCESS;
  } else {
    LOG(ERROR) << "FAILED";
    return EXIT_FAILURE;
  }
}
