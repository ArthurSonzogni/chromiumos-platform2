// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "oobe_config/features/features.h"

#include <base/check.h>
#include <base/logging.h>
#include <dbus/bus.h>
#include <featured/feature_library.h>

namespace oobe_config {
namespace {
const struct VariationsFeature kEnterpriseRollbackUseTpmEncryption = {
    .name = "CrOSLateBootEnterpriseRollbackUseTpmEncryption",
    // Default is enabled. This is a kill switch.
    .default_state = FEATURE_ENABLED_BY_DEFAULT,
};
}  // namespace

bool TpmEncryptionFeatureEnabled() {
  dbus::Bus::Options options;
  options.bus_type = dbus::Bus::SYSTEM;
  scoped_refptr<dbus::Bus> bus(new dbus::Bus(options));

  // TODO(b:263065223) Make this a CHECK once we have sufficient integration
  // test signal that it doesn't fail.
  if (!feature::PlatformFeatures::Initialize(bus)) {
    LOG(ERROR) << "Failed to initialize dbus.";
    return false;
  }

  feature::PlatformFeatures* feature_lib = feature::PlatformFeatures::Get();
  bool enabled =
      feature_lib->IsEnabledBlocking(kEnterpriseRollbackUseTpmEncryption);
  LOG(INFO) << "Tpm encryption feature is enabled: "
            << (enabled ? "yes" : "no");
  return enabled;
}

}  // namespace oobe_config
