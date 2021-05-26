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
const char kSysfsNvmemDevicesRoot[] = "/sys/bus/nvmem/devices";
const char kVendorIdPath[] = "device/vendor_id";
const std::vector<std::string> kArgsPattern = {"modules", "list"};
const size_t kEepromIdBlockAlignment = 32u;

struct EepromIdBlock {
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

struct EepromInfo {
  EepromIdBlock id_block;
  base::FilePath nvmem_path;
};

struct V4L2SensorInfo {
  std::string name;
  std::string vendor_id;
  base::FilePath subdev_path;
};

bool ValidateCameraModuleInfo(base::span<const uint8_t> section) {
  if (section.size() < sizeof(EepromIdBlock)) {
    return false;
  }
  auto* info = reinterpret_cast<const EepromIdBlock*>(section.data());
  const uint16_t crc =
      Crc16CcittFalse(section.subspan(offsetof(EepromIdBlock, version)), 0u);
  return strncmp(info->os, "CrOS", 4) == 0 && info->crc == crc &&
         info->version == 1u;
}

base::Optional<EepromIdBlock> FindCameraEepromIdBlock(const std::string& mem) {
  static_assert(sizeof(EepromIdBlock) <= kEepromIdBlockAlignment);
  const size_t alignment = kEepromIdBlockAlignment;
  const uint8_t* data_end =
      reinterpret_cast<const uint8_t*>(mem.data()) + mem.size();
  for (size_t offset_from_end = alignment + mem.size() % alignment;
       offset_from_end <= mem.size(); offset_from_end += alignment) {
    base::span<const uint8_t> section =
        base::make_span(data_end - offset_from_end, sizeof(EepromIdBlock));
    if (ValidateCameraModuleInfo(section)) {
      return *reinterpret_cast<const EepromIdBlock*>(section.data());
    }
  }
  return base::nullopt;
}

class CameraTool {
 private:
  struct Camera {
    const EepromInfo* eeprom;
    const V4L2SensorInfo* v4l2_sensor;
    std::string sysfs_name;
  };

 public:
  void PrintCameras() {
    const std::vector<Camera> cameras = GetPlatformCameras();

    base::Value root(base::Value::Type::LIST);
    for (const auto& camera : cameras) {
      base::Value node(base::Value::Type::DICTIONARY);
      if (camera.eeprom != nullptr) {
        const EepromIdBlock& b = camera.eeprom->id_block;
        node.SetStringKey("name", camera.sysfs_name);
        node.SetStringKey("module_id",
                          base::StringPrintf("%c%c%04x", b.module_vid[0],
                                             b.module_vid[1], b.module_pid));
        node.SetStringKey("sensor_id",
                          base::StringPrintf("%c%c%04x", b.sensor_vid[0],
                                             b.sensor_vid[1], b.sensor_pid));
      } else {
        CHECK_NE(camera.v4l2_sensor, nullptr);
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
  void ProbeSensorSubdev(struct media_entity_desc* desc,
                         const base::FilePath& path) {
    V4L2SensorInfo sensor{.name = desc->name};
    std::string vendor_id;
    const base::FilePath& vendor_id_path = path.Append(kVendorIdPath);
    if (base::ReadFileToStringWithMaxSize(vendor_id_path, &vendor_id, 64)) {
      base::TrimWhitespaceASCII(vendor_id, base::TRIM_ALL, &sensor.vendor_id);
    }
    sensor.subdev_path = base::MakeAbsoluteFilePath(path);
    LOG(INFO) << "Found V4L2 sensor subdev on " << sensor.subdev_path;

    v4l2_sensors_.emplace_back(std::move(sensor));
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

  void AddV4L2Sensors() {
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

  void AddCameraEeproms() {
    base::FileEnumerator dev_enum(base::FilePath(kSysfsNvmemDevicesRoot), false,
                                  base::FileEnumerator::DIRECTORIES);
    for (base::FilePath dev_path = dev_enum.Next(); !dev_path.empty();
         dev_path = dev_enum.Next()) {
      const base::FilePath nvmem_path =
          base::MakeAbsoluteFilePath(dev_path.Append("nvmem"));
      if (nvmem_path.empty()) {
        LOG(ERROR) << "Failed to resolve absolute nvmem path from " << dev_path;
        continue;
      }
      std::string nvmem;
      if (!base::ReadFileToString(nvmem_path, &nvmem)) {
        LOG(ERROR) << "Failed to read nvmem from " << nvmem_path;
        continue;
      }
      base::Optional<EepromIdBlock> id_block = FindCameraEepromIdBlock(nvmem);
      if (!id_block.has_value())
        continue;
      LOG(INFO) << "Found camera eeprom on " << nvmem_path;
      eeproms_.push_back(EepromInfo{
          .id_block = *id_block,
          .nvmem_path = std::move(nvmem_path),
      });
    }
  }

  std::vector<Camera> GetPlatformCameras() {
    if (eeproms_.empty())
      AddCameraEeproms();
    if (v4l2_sensors_.empty())
      AddV4L2Sensors();

    // Associate probed nvmems and v4l-subdevs by their absolute sysfs device
    // paths. When both devices exist, they are expected to locate on the same
    // I2C bus. For example:
    //   /path/to/i2c/sysfs - i2c-2 - 2-0010 - video4linux - v4l-subdev6
    //                             \- 2-0058 - 2-00580 - nvmem
    std::vector<Camera> cameras;
    std::set<const V4L2SensorInfo*> associated_sensors;
    for (const EepromInfo& eeprom : eeproms_) {
      std::vector<std::string> path;
      eeprom.nvmem_path.GetComponents(&path);
      CHECK_GE(path.size(), 4u);
      auto iter = std::find_if(v4l2_sensors_.begin(), v4l2_sensors_.end(),
                               [&](const V4L2SensorInfo& sensor) {
                                 std::vector<std::string> p;
                                 sensor.subdev_path.GetComponents(&p);
                                 return std::equal(path.begin(), path.end() - 3,
                                                   p.begin());
                               });
      const V4L2SensorInfo* sensor =
          iter != v4l2_sensors_.end() ? &*iter : nullptr;
      cameras.push_back(Camera{
          .eeprom = &eeprom,
          .v4l2_sensor = sensor,
          .sysfs_name = path[path.size() - 4] + '/' + path[path.size() - 3],
      });
      if (sensor != nullptr)
        associated_sensors.insert(sensor);
    }
    for (const V4L2SensorInfo& sensor : v4l2_sensors_) {
      if (!base::Contains(associated_sensors, &sensor)) {
        cameras.push_back(Camera{
            .v4l2_sensor = &sensor,
        });
      }
    }
    return cameras;
  }

 private:
  std::vector<EepromInfo> eeproms_;
  std::vector<V4L2SensorInfo> v4l2_sensors_;
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
