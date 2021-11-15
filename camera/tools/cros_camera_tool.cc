/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <algorithm>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <base/at_exit.h>
#include <base/command_line.h>
#include <base/json/json_writer.h>
#include <base/logging.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <base/values.h>
#include <brillo/syslog_logging.h>

#include "cros-camera/device_config.h"

namespace {

const std::vector<std::string> kArgsPattern = {"modules", "list"};

class CameraTool {
 public:
  CameraTool() : device_config_(cros::DeviceConfig::Create()) {}

  void PrintCameras() {
    if (!device_config_) {
      LOG(ERROR) << "No camera found on device";
      return;
    }

    base::span<const cros::PlatformCameraInfo> cameras =
        device_config_->GetPlatformCameraInfo();
    base::Value root(base::Value::Type::LIST);
    for (const auto& camera : cameras) {
      base::Value node(base::Value::Type::DICTIONARY);
      if (camera.eeprom) {
        const cros::EepromIdBlock& b = camera.eeprom->id_block;
        node.SetStringKey("name", camera.sysfs_name);
        node.SetStringKey("module_id",
                          base::StringPrintf("%c%c%04x", b.module_vid[0],
                                             b.module_vid[1], b.module_pid));
        node.SetStringKey("sensor_id",
                          base::StringPrintf("%c%c%04x", b.sensor_vid[0],
                                             b.sensor_vid[1], b.sensor_pid));
      } else {
        CHECK(camera.v4l2_sensor.has_value());
        node.SetStringKey("name", camera.v4l2_sensor->name);
        node.SetStringKey("vendor", camera.v4l2_sensor->vendor_id);
      }
      root.Append(std::move(node));
    }
    std::string json;
    if (!base::JSONWriter::WriteWithOptions(
            root, base::JSONWriter::OPTIONS_PRETTY_PRINT, &json)) {
      LOG(ERROR) << "Failed to print camera infos";
    }
    std::cout << json << std::endl;
  }

 private:
  base::Optional<cros::DeviceConfig> device_config_;
};

bool StringEqualsCaseInsensitiveASCII(const std::string& a,
                                      const std::string& b) {
  return base::EqualsCaseInsensitiveASCII(a, b);
}

}  // namespace

int main(int argc, char* argv[]) {
  // Init CommandLine for InitLogging.
  base::CommandLine::Init(argc, argv);
  base::AtExitManager at_exit_manager;
  brillo::InitLog(brillo::kLogToSyslog | brillo::kLogToStderrIfTty);

  // FIXME: Currently only "modules list" command is supported
  base::CommandLine* cl = base::CommandLine::ForCurrentProcess();
  const std::vector<std::string>& args = cl->GetArgs();
  if (cl->GetArgs().empty() ||
      !std::equal(kArgsPattern.begin(), kArgsPattern.end(), args.begin(),
                  StringEqualsCaseInsensitiveASCII)) {
    LOG(ERROR) << "Invalid command.";
    LOG(ERROR) << "Try following supported commands:";
    LOG(ERROR) << "  modules - operations on camera modules";
    LOG(ERROR) << "    list - print available modules";
    return 1;
  }

  CameraTool tool;
  tool.PrintCameras();

  return 0;
}
