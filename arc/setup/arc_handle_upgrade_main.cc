// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include <base/at_exit.h>
#include <base/check.h>
#include <base/command_line.h>
#include <base/files/file_path.h>
#include <base/timer/elapsed_timer.h>
#include <base/logging.h>
#include <brillo/flag_helper.h>
#include <brillo/syslog_logging.h>

#include "arc/setup/arc_setup.h"
#include "arc/setup/arc_setup_util.h"

int main(int argc, char** argv) {
  DEFINE_string(log_tag, "", "Tag to be used in syslog");
  DEFINE_string(data_type, "",
                "Type of /data: virtiofs | lvm_volume | concierge_disk");

  base::ElapsedTimer timer;
  base::AtExitManager at_exit;

  brillo::FlagHelper::Init(argc, argv, "Handle ARC upgrades");

  CHECK(!FLAGS_log_tag.empty()) << "Must specify --log_tag";
  CHECK(!FLAGS_data_type.empty()) << "Must specify --data_type";

  brillo::OpenLog(FLAGS_log_tag.c_str(), true /*log_pid*/);
  brillo::InitLog(brillo::kLogToSyslog | brillo::kLogHeader |
                  brillo::kLogToStderrIfTty);

  arc::ArcVmDataType data_type;
  if (FLAGS_data_type == "virtiofs") {
    data_type = arc::ArcVmDataType::kVirtiofs;
  } else if (FLAGS_data_type == "lvm_volume") {
    data_type = arc::ArcVmDataType::kLvmVolume;
  } else if (FLAGS_data_type == "concierge_disk") {
    data_type = arc::ArcVmDataType::kConciergeDisk;
  } else {
    LOG(FATAL) << "Invalid /data type: " << FLAGS_data_type;
  }

  const std::string command_line =
      base::CommandLine::ForCurrentProcess()->GetCommandLineString();
  LOG(INFO) << "Starting " << command_line;
  arc::ArcSetup(arc::Mode::HANDLE_UPGRADE, data_type).Run();
  LOG(INFO) << command_line << " took "
            << timer.Elapsed().InMillisecondsRoundedUp() << "ms";
  return 0;
}
