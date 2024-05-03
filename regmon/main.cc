// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sysexits.h>

#include <base/check.h>
#include <base/logging.h>

#include <brillo/syslog_logging.h>
#include <dbus/bus.h>
#include <featured/feature_library.h>

#include "regmon/daemon/regmon_daemon.h"
#include "regmon/features/regmon_features_impl.h"

namespace {

constexpr char kUsage[] = R"(
Usage: regmond
)";

void SetLogItems() {
  const bool kOptionPID = true;
  const bool kOptionTID = true;
  const bool kOptionTimestamp = true;
  const bool kOptionTickcount = true;
  logging::SetLogItems(kOptionPID, kOptionTID, kOptionTimestamp,
                       kOptionTickcount);
}

}  // namespace

int main(int argc, char* argv[]) {
  if (argc != 1) {
    LOG(ERROR) << "regmond: too many arguments.\n" << kUsage;
    return EX_USAGE;
  }

  // Always log to syslog and log to stderr if we are connected to a tty.
  brillo::InitLog(brillo::kLogToSyslog | brillo::kLogToStderrIfTty);

  dbus::Bus::Options options;
  options.bus_type = dbus::Bus::SYSTEM;
  scoped_refptr<dbus::Bus> bus(new dbus::Bus(options));

  CHECK(feature::PlatformFeatures::Initialize(bus));
  ::regmon::features::RegmonFeaturesImpl regmon_features(
      feature::PlatformFeatures::Get());
  if (!regmon_features.PolicyMonitoringEnabled()) {
    LOG(INFO) << "regmond: Feature not enabled.\n";
    return EX_UNAVAILABLE;
  }

  // Override the log items set by brillo:InitLog.
  SetLogItems();

  LOG(INFO) << "Starting Regmon Service.";
  const int exit_code = ::regmon::RegmonDaemon().Run();
  LOG(INFO) << "Regmon Service ended with exit_code=" << exit_code;

  return exit_code;
}
