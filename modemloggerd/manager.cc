// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "modemloggerd/manager.h"

#include <memory>
#include <string>
#include <unordered_map>

#include <brillo/proto_file_io.h>
#include <cros_config/cros_config.h>

#include "modemloggerd/helper_manifest.pb.h"
#include "modemloggerd/modem.h"

namespace {

constexpr char const* kDevicesSupportingLogging[] = {"em060", "fm101", "rw101",
                                                     "nl668", "fm350", "rw350"};

const char kManifest[] =
    "/usr/local/usr/share/modemloggerd/helper_manifest.textproto";

std::string GetModemName() {
  brillo::CrosConfig config;
  std::string fw_variant;
  if (!config.GetString("/modem", "firmware-variant", &fw_variant)) {
    LOG(INFO)
        << "No modem firmware variant is specified. Cannot parse modem name.";
    return std::string();
  }
  // Old modem variants did not include the modem name in them. Check for those
  // variants using a local map.
  // Old L850 variants are skipped since L850 is not supported.
  auto variant_to_modem = std::unordered_map<std::string, std::string>({
      {"vilboz", "nl668"},
  });

  const auto it = variant_to_modem.find(fw_variant);
  if (it != variant_to_modem.end()) {
    return it->second;
  }

  // TODO(b/312535821): Use udev/MM instead of cros_config for modem detection
  for (auto modem : kDevicesSupportingLogging) {
    if (fw_variant.find(modem) != std::string::npos) {
      return modem;
    }
  }
  LOG(INFO) << "`" << fw_variant << "`  does not support modem logging";
  return std::string();
}

}  // namespace

namespace modemloggerd {

Manager::Manager(dbus::Bus* bus, AdaptorFactoryInterface* adaptor_factory)
    : bus_(bus),
      dbus_adaptor_(adaptor_factory->CreateManagerAdaptor(this, bus_)) {
  LOG(INFO) << __func__;
  HelperManifest parsed_manifest;
  if (!brillo::ReadTextProtobuf(base::FilePath(kManifest), &parsed_manifest)) {
    LOG(ERROR) << "Could not parse helper manifest";
    return;
  }
  auto modem_name = GetModemName();
  for (const HelperEntry& entry : parsed_manifest.helper()) {
    if (entry.modem_name() == modem_name) {
      // TODO(b/312535821): Introduce DBus method so that MM tells when a modem
      // has been found/ monitor udev
      available_modems_.push_back(
          std::make_unique<Modem>(bus_, adaptor_factory, entry));
      break;
    }
  }
  UpdateAvailableModemsProperty();
}

void Manager::UpdateAvailableModemsProperty() {
  LOG(INFO) << __func__;
  std::vector<dbus::ObjectPath> modem_paths;
  for (const auto& modem : available_modems_) {
    modem_paths.push_back(modem->GetDBusPath());
  }
  dbus_adaptor_->SetAvailableModems(modem_paths);
}

}  // namespace modemloggerd
