/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <fcntl.h>
#include <linux/media.h>
#include <sys/ioctl.h>

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <base/at_exit.h>
#include <base/command_line.h>
#include <base/containers/span.h>
#include <base/files/file_enumerator.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/json/json_writer.h>
#include <base/optional.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <base/values.h>
#include <brillo/syslog_logging.h>

#include "tools/crc_ccitt.h"

namespace {

const char kSysfsV4lClassRoot[] = "/sys/class/video4linux";
const char kSysfsI2cDevicesRoot[] = "/sys/bus/i2c/devices";
const char kVendorIdPath[] = "device/vendor_id";
const std::vector<std::string> kArgsPattern = {"modules", "list"};
const size_t kEepromIdBlockAlignment = 32u;

struct eeprom_id_block {
  char os[4];
  uint16_t crc;
  uint8_t version;
  uint8_t length;
  uint16_t data_format;
  uint16_t module_pid;
  char module_vid[2];
  char sensor_vid[2];
  uint16_t sensor_pid;
};

struct camera {
  std::string name;
  std::string vendor_id;
  base::Optional<eeprom_id_block> eeprom;
};

bool ValidateCameraModuleInfo(base::span<const uint8_t> section) {
  if (section.size() < sizeof(eeprom_id_block)) {
    return false;
  }
  auto* info = reinterpret_cast<const eeprom_id_block*>(section.data());
  const uint16_t crc =
      Crc16CcittFalse(section.subspan(offsetof(eeprom_id_block, version)), 0u);
  return strncmp(info->os, "CrOS", 4) == 0 && info->crc == crc &&
         info->version == 1u;
}

base::FilePath FindEepromPathForSensorSubdev(const std::string& v4l_subdev) {
  // Finds the eeprom node that is on the same I2C bus as the sensor V4L
  // subdevice. For example:
  //   /sys/bus/i2c/devices - i2c-2 - 2-0010 - video4linux - v4l-subdev6
  //                               \- 2-0058 - eeprom
  base::FileEnumerator bus_enum(base::FilePath(kSysfsI2cDevicesRoot), false,
                                base::FileEnumerator::DIRECTORIES);
  for (base::FilePath bus_path = bus_enum.Next(); !bus_path.empty();
       bus_path = bus_enum.Next()) {
    base::FileEnumerator dev_enum(bus_path, false,
                                  base::FileEnumerator::DIRECTORIES);
    bool found_v4l_subdev = false;
    base::FilePath eeprom_path;
    for (base::FilePath dev_path = dev_enum.Next(); !dev_path.empty();
         dev_path = dev_enum.Next()) {
      if (base::PathExists(dev_path.Append("video4linux").Append(v4l_subdev))) {
        found_v4l_subdev = true;
      }
      base::FilePath path = dev_path.Append("eeprom");
      if (base::PathExists(path)) {
        eeprom_path = path;
      }
    }
    if (found_v4l_subdev && !eeprom_path.empty()) {
      return eeprom_path;
    }
  }
  return base::FilePath{};
}

base::Optional<eeprom_id_block> ReadEepromForSensorSubdev(
    const std::string& v4l_subdev) {
  const base::FilePath eeprom_path = FindEepromPathForSensorSubdev(v4l_subdev);
  if (eeprom_path.empty()) {
    return base::nullopt;
  }
  LOG(INFO) << "Found EEPROM path " << eeprom_path << " for " << v4l_subdev;

  std::string eeprom;
  if (!base::ReadFileToString(eeprom_path, &eeprom)) {
    LOG(ERROR) << "Failed to read EEPROM from sysfs";
    return base::nullopt;
  }

  static_assert(sizeof(eeprom_id_block) <= kEepromIdBlockAlignment);
  const size_t alignment = kEepromIdBlockAlignment;
  const uint8_t* data_end =
      reinterpret_cast<const uint8_t*>(eeprom.data()) + eeprom.size();
  for (size_t offset_from_end = alignment + eeprom.size() % alignment;
       offset_from_end <= eeprom.size(); offset_from_end += alignment) {
    base::span<const uint8_t> section =
        base::make_span(data_end - offset_from_end, sizeof(eeprom_id_block));
    if (ValidateCameraModuleInfo(section)) {
      return *reinterpret_cast<const eeprom_id_block*>(section.data());
    }
  }
  LOG(INFO) << "Didn't find module identification block in EEPROM data";
  return base::nullopt;
}

class CameraTool {
  typedef std::vector<struct camera> CameraVector;

 public:
  void PrintCameras(void) {
    const CameraVector& cameras = GetPlatformCameras();

    if (cameras.empty()) {
      std::cout << "No cameras detected in the system." << std::endl;
      return;
    }

    base::Value root(base::Value::Type::LIST);
    for (const auto& camera : cameras) {
      base::Value node(base::Value::Type::DICTIONARY);
      node.SetStringKey("name", camera.name);
      if (camera.eeprom.has_value()) {
        const struct eeprom_id_block& b = *camera.eeprom;
        node.SetStringKey("module_id",
                          base::StringPrintf("%c%c%04x", b.module_vid[0],
                                             b.module_vid[1], b.module_pid));
        node.SetStringKey("sensor_id",
                          base::StringPrintf("%c%c%04x", b.sensor_vid[0],
                                             b.sensor_vid[1], b.sensor_pid));
      } else {
        node.SetStringKey("vendor", camera.vendor_id);
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
  void ProbeSensorSubdev(struct media_entity_desc* desc,
                         const base::FilePath& path) {
    struct camera camera {
      .name = desc->name
    };
    std::string vendor_id;
    const base::FilePath& vendor_id_path = path.Append(kVendorIdPath);
    if (base::ReadFileToStringWithMaxSize(vendor_id_path, &vendor_id, 64)) {
      base::TrimWhitespaceASCII(vendor_id, base::TRIM_ALL, &camera.vendor_id);
    }
    camera.eeprom = ReadEepromForSensorSubdev(path.BaseName().value());

    platform_cameras_.emplace_back(std::move(camera));
  }

  base::FilePath FindSubdevSysfsByDevId(int major, int minor) {
    base::FileEnumerator dev_enum(base::FilePath(kSysfsV4lClassRoot), false,
                                  base::FileEnumerator::DIRECTORIES,
                                  "v4l-subdev*");
    for (base::FilePath name = dev_enum.Next(); !name.empty();
         name = dev_enum.Next()) {
      base::FilePath dev_path = name.Append("dev");
      std::string dev_id("255:255");
      if (!base::ReadFileToStringWithMaxSize(dev_path, &dev_id,
                                             dev_id.size())) {
        LOG(ERROR) << "Failed to read device ID of '" << dev_path.value()
                   << "' from sysfs";
        continue;
      }
      base::TrimWhitespaceASCII(dev_id, base::TRIM_ALL, &dev_id);

      std::ostringstream stream;
      stream << major << ":" << minor;
      if (dev_id == stream.str())
        return name;
    }

    return base::FilePath();
  }

  void ProbeMediaController(int media_fd) {
    struct media_entity_desc desc;

    for (desc.id = MEDIA_ENT_ID_FLAG_NEXT;
         !ioctl(media_fd, MEDIA_IOC_ENUM_ENTITIES, &desc);
         desc.id |= MEDIA_ENT_ID_FLAG_NEXT) {
      if (desc.type != MEDIA_ENT_T_V4L2_SUBDEV_SENSOR)
        continue;

      const base::FilePath& path =
          FindSubdevSysfsByDevId(desc.dev.major, desc.dev.minor);
      if (path.empty()) {
        LOG(ERROR) << "v4l-subdev node for sensor '" << desc.name
                   << "' not found";
        continue;
      }

      LOG(INFO) << "Probing sensor '" << desc.name << "' ("
                << path.BaseName().value() << ")";
      ProbeSensorSubdev(&desc, path);
    }
  }

  void AddV4l2Cameras(void) {
    base::FileEnumerator dev_enum(base::FilePath("/dev"), false,
                                  base::FileEnumerator::FILES, "media*");
    for (base::FilePath name = dev_enum.Next(); !name.empty();
         name = dev_enum.Next()) {
      int fd = open(name.value().c_str(), O_RDWR);
      if (fd < 0) {
        LOG(ERROR) << "Failed to open '" << name.value() << "'";
        continue;
      }

      LOG(INFO) << "Probing media device '" << name.value() << "'";
      ProbeMediaController(fd);
      close(fd);
    }
  }

  const CameraVector& GetPlatformCameras() {
    if (platform_cameras_.empty())
      AddV4l2Cameras();

    return platform_cameras_;
  }

  CameraVector platform_cameras_;
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
