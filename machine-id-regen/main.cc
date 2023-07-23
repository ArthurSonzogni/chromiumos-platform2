// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>

#include <base/logging.h>
#include <base/time/time.h>
#include <brillo/flag_helper.h>
#include <brillo/syslog_logging.h>
#include <metrics/metrics_library.h>

#include "machine-id-regen/machine_id_regen.h"

namespace {
constexpr char kDefaultMachineIdFile[] = "/var/lib/dbus/machine-id";
constexpr char kStateDir[] = "/run/cros-machine-id-regen";
}  // namespace

int main(int argc, char* argv[]) {
  DEFINE_string(reason, "",
                "ID regeneration reason. "
                "'network' or 'period'.");
  DEFINE_uint32(minimum_age, 0,
                "Don't regenerate if last regeneration "
                "was this many seconds ago.");
  DEFINE_string(machine_id_file, kDefaultMachineIdFile,
                "Path to machine-id file to use instead of default");

  brillo::FlagHelper::Init(argc, argv, "Regenerate machine id file.");
  brillo::InitLog(brillo::kLogToSyslog | brillo::kLogToStderrIfTty);

  if (FLAGS_reason.empty()) {
    LOG(ERROR) << "Reason is empty";
    return 1;
  }

  std::shared_ptr<MetricsLibrary> metrics_lib =
      std::make_shared<MetricsLibrary>();
  metrics_lib->Init();

  base::TimeDelta minimum_age = base::Seconds(FLAGS_minimum_age * 1000 * 1000);
  if (!machineidregen::regen_machine_id(
          base::FilePath(kStateDir), base::FilePath(FLAGS_machine_id_file),
          FLAGS_reason, metrics_lib, minimum_age)) {
    return 1;
  }

  return 0;
}
