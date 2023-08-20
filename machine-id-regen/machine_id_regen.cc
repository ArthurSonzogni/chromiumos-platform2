// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "machine-id-regen/machine_id_regen.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/logging.h>
#include <base/rand_util.h>
#include <base/strings/string_split.h>
#include <base/system/sys_info.h>
#include <brillo/dbus/dbus_method_invoker.h>
#include <brillo/dbus/dbus_signal.h>
#include <brillo/errors/error.h>
#include <brillo/syslog_logging.h>
#include <dbus/error.h>
#include <sys/stat.h>
#include <sys/file.h>

#include "machine-id-regen/file_auto_lock.h"
#include "machine-id-regen/timestamp.h"
#include "upstart/dbus-proxies.h"

namespace {

constexpr char kTimestampFileName[] = "timestamp-machine-id";
constexpr char kUpstartServiceName[] = "com.ubuntu.Upstart";
constexpr char kRegenEventName[] = "cros-machine-id-regenerated";
constexpr char kAvahiServiceName[] = "org.freedesktop.Avahi";
constexpr char kAvahiInterfaceName[] = "org.freedesktop.Avahi.Server";
constexpr char kAvahiMethodName[] = "SetHostName";

struct MetricsDescription {
  const std::string reason;
  const std::string pretty;
  const int value;
  MetricsDescription() : reason("unknown"), pretty("Unknown"), value(0) {}
  MetricsDescription(const std::string& reason,
                     const std::string& pretty,
                     int value)
      : reason(reason), pretty(pretty), value(value) {}
};

bool emit_metrics(const std::string& reason,
                  std::shared_ptr<MetricsLibrary> metrics_lib,
                  base::TimeDelta last_update_s) {
  std::vector<std::shared_ptr<MetricsDescription>> reasons = {
      std::make_shared<MetricsDescription>("network", "Network", 1),
      std::make_shared<MetricsDescription>("periodic", "Periodic", 2)};
  std::vector<std::shared_ptr<MetricsDescription>>::iterator it = std::find_if(
      reasons.begin(), reasons.end(),
      [&reason](const std::shared_ptr<MetricsDescription>& metrics) {
        return metrics->reason.compare(reason);
      });
  std::shared_ptr<MetricsDescription> metrics = nullptr;
  if (it == reasons.end()) {
    metrics = std::make_shared<MetricsDescription>();
  } else {
    metrics = *it;
  }

  if (!metrics_lib->SendSparseToUMA("ChromeOS.MachineIdRegen.Reason",
                                    metrics->value)) {
    return false;
  }

  if (last_update_s.is_zero()) {
    return false;
  }

  return metrics_lib->SendToUMA("ChromeOS.MachineIdRegen.AgeSeconds",
                                last_update_s.InSeconds(), 0, 86400, 50) &&
         metrics_lib->SendToUMA(
             "ChromeOS.MachineIdRegen.AgeSeconds_" + metrics->pretty,
             last_update_s.InSeconds(), 0, 86400, 50);
}

std::string generate_machine_id() {
  uint8_t machine_id[16];

  base::RandBytes(machine_id, sizeof(machine_id));
  return base::HexEncode(machine_id, sizeof(machine_id));
}

}  // namespace

namespace machineidregen {

bool send_machine_id_to_avahi(scoped_refptr<dbus::Bus> bus,
                              const std::string& machine_id) {
  dbus::ObjectProxy* proxy =
      bus->GetObjectProxy(kAvahiServiceName, dbus::ObjectPath("/"));
  if (!proxy) {
    LOG(WARNING) << "Unable to get dbus proxy for " << kAvahiServiceName;
    return false;
  }

  dbus::MethodCall method_call(kAvahiInterfaceName, kAvahiMethodName);
  dbus::MessageWriter writer(&method_call);
  std::vector<std::string> args_keyvals;
  args_keyvals.emplace_back(machine_id);
  writer.AppendArrayOfStrings(args_keyvals);

  auto result = proxy->CallMethodAndBlock(&method_call, 10000);
  if (!result.has_value()) {
    dbus::Error error = std::move(result.error());
    std::string error_name = error.name();
    LOG(WARNING) << kAvahiMethodName << " finished with " << error_name
                 << " error.";
    return false;
  }

  return true;
}

bool emit_machine_id_regen(scoped_refptr<dbus::Bus> bus) {
  brillo::ErrorPtr error;
  com::ubuntu::Upstart0_6Proxy upstart_proxy =
      com::ubuntu::Upstart0_6Proxy(bus, kUpstartServiceName);
  if (!upstart_proxy.EmitEvent(kRegenEventName, {}, false, &error)) {
    LOG(ERROR) << "Could not emit upstart event: " << error->GetMessage();
    return false;
  }

  return true;
}

bool regen_machine_id(const base::FilePath& state_dir,
                      const base::FilePath& machine_id_file,
                      const std::string& reason,
                      std::shared_ptr<MetricsLibrary> metrics_lib,
                      base::TimeDelta minimum_age_seconds) {
  FileAutoLock lock(base::FilePath(state_dir.value() + "/lock"));
  if (!lock.lock()) {
    LOG(ERROR) << "Could not aquire lock";
    return false;
  }

  base::TimeDelta uptime = base::SysInfo::Uptime();

  Timestamp timestamp(state_dir.Append(kTimestampFileName));
  base::TimeDelta last_update;
  std::optional<base::TimeDelta> ret = timestamp.get_last_update();
  if (ret.has_value()) {
    last_update = ret.value();
  } else {
    LOG(WARNING) << "Could not read last regeneration time from "
                 << timestamp.GetPath();
    LOG(WARNING) << "Reset last update time to 0";
    last_update = base::Seconds(0);
  }

  base::TimeDelta seconds_since_last_update = uptime - last_update;
  if (!minimum_age_seconds.is_zero() && !seconds_since_last_update.is_zero() &&
      seconds_since_last_update < minimum_age_seconds) {
    LOG(INFO) << "Not regenerating since we did so "
              << seconds_since_last_update.InSeconds() << " seconds ago";
    return true;
  }

  const std::string new_machine_id = generate_machine_id() + "\n";
  if (!brillo::WriteToFileAtomic(machine_id_file, new_machine_id.c_str(),
                                 new_machine_id.length(), 0644)) {
    PLOG(ERROR) << "Failed to save to machine id to " << machine_id_file;
    return false;
  }

  if (!timestamp.update(uptime)) {
    LOG(ERROR) << "Could not update regeneration timestamp in "
               << timestamp.GetPath();
    return false;
  }

  dbus::Bus::Options opts;
  opts.bus_type = dbus::Bus::SYSTEM;
  scoped_refptr<dbus::Bus> bus(new dbus::Bus(std::move(opts)));
  if (!bus->Connect()) {
    LOG(ERROR) << "Failed to connect to system dbus";
    return false;
  }

  if (!send_machine_id_to_avahi(bus, new_machine_id)) {
    LOG(WARNING) << "Skip Avahi update assuming avahi deamon is offline.";
  }

  if (!emit_machine_id_regen(bus)) {
    LOG(ERROR) << "emit failed";
  }

  LOG(INFO) << "Regenerated " << machine_id_file
            << " (reason: " + reason + ") ";

  bool emit_result =
      emit_metrics(reason, metrics_lib, seconds_since_last_update);
  if (!emit_result) {
    LOG(ERROR) << "Error while emitting metrics";
    return false;
  }

  if (!lock.unlock()) {
    LOG(ERROR) << "Could not release lock";
    return false;
  }

  return true;
}

}  // namespace machineidregen
