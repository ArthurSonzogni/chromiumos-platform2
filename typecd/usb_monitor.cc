// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "typecd/usb_monitor.h"

#include <utility>

#include <base/files/file_util.h>
#include <base/logging.h>
#include "base/strings/string_number_conversions.h"
#include <base/strings/string_util.h>
#include <re2/re2.h>

namespace {
constexpr char kInterfaceFilePathRegex[] =
    R"((\d+)-(\d+)(\.(\d+))*:(\d+)\.(\d+))";
constexpr char kTypecPortUeventRegex[] = R"(TYPEC_PORT=port(\d+))";

typecd::UsbSpeed ConvertToUsbSpeed(std::string speed) {
  if (speed == "1.5")
    return typecd::UsbSpeed::k1_5;
  else if (speed == "12")
    return typecd::UsbSpeed::k12;
  else if (speed == "480")
    return typecd::UsbSpeed::k480;
  else if (speed == "5000")
    return typecd::UsbSpeed::k5000;
  else if (speed == "10000")
    return typecd::UsbSpeed::k10000;
  else if (speed == "20000")
    return typecd::UsbSpeed::k20000;
  else
    return typecd::UsbSpeed::kOther;
}

typecd::UsbDeviceClass ConvertToUsbClass(std::string device_class) {
  if (device_class == "00")
    return typecd::UsbDeviceClass::kNone;
  else if (device_class == "09")
    return typecd::UsbDeviceClass::kHub;
  else
    return typecd::UsbDeviceClass::kOther;
}

// Convert version string parsed from USB device sysfs to UsbVersion enum to
// store in UsbDevice.
typecd::UsbVersion ConvertToUsbVersion(std::string version) {
  if (version == "1.00")
    return typecd::UsbVersion::k1_0;
  else if (version == "1.10")
    return typecd::UsbVersion::k1_1;
  else if (version == "2.00")
    return typecd::UsbVersion::k2_0;
  else if (version == "2.10")
    return typecd::UsbVersion::k2_1;
  else if (version == "3.00")
    return typecd::UsbVersion::k3_0;
  else if (version == "3.10")
    return typecd::UsbVersion::k3_1;
  else if (version == "3.20")
    return typecd::UsbVersion::k3_2;
  else
    return typecd::UsbVersion::kOther;
}

}  // namespace

namespace typecd {

UsbMonitor::UsbMonitor() : metrics_(nullptr) {}

void UsbMonitor::OnDeviceAddedOrRemoved(const base::FilePath& path,
                                        bool added) {
  auto key = path.BaseName().value();
  if (RE2::FullMatch(key, kInterfaceFilePathRegex)) {
    return;
  }

  auto it = devices_.find(key);
  if (added) {
    if (it != devices_.end()) {
      LOG(WARNING) << "Attempting to add an already added usb device in "
                   << path;
      return;
    }

    std::string busnum;
    std::string devnum;
    if (!base::ReadFileToString(path.Append("busnum"), &busnum)) {
      PLOG(ERROR) << "Failed to find busnum in " << path;
      return;
    }
    if (!base::ReadFileToString(path.Append("devnum"), &devnum)) {
      PLOG(ERROR) << "Failed to find devnum in " << path;
      return;
    }
    base::TrimWhitespaceASCII(busnum, base::TRIM_TRAILING, &busnum);
    base::TrimWhitespaceASCII(devnum, base::TRIM_TRAILING, &devnum);

    int busnum_int;
    int devnum_int;
    if (!base::StringToInt(busnum, &busnum_int) ||
        !base::StringToInt(devnum, &devnum_int)) {
      PLOG(ERROR) << "Failed to parse integer value from busnum and devnum in "
                  << path;
      return;
    }
    devices_.emplace(key,
                     std::make_unique<UsbDevice>(busnum_int, devnum_int, key));

    std::string typec_port_uevent;
    int typec_port_num;
    if (base::ReadFileToString(path.Append("port/connector/uevent"),
                               &typec_port_uevent) &&
        RE2::PartialMatch(typec_port_uevent, kTypecPortUeventRegex,
                          &typec_port_num)) {
      GetDevice(key)->SetTypecPortNum(typec_port_num);
    } else {
      // Parent USB hub device's sysfs directory name.
      // e.g. Parent sysfs of 2-1.5.4 would be 2-1
      auto parent_key = key.substr(0, key.find("."));

      // If parent USB hub device is present and has a Type C port associated,
      // use the parent's Type C port number.
      if (GetDevice(parent_key) != nullptr)
        GetDevice(key)->SetTypecPortNum(
            GetDevice(parent_key)->GetTypecPortNum());
    }

    std::string speed;
    if (base::ReadFileToString(path.Append("speed"), &speed)) {
      base::TrimWhitespaceASCII(speed, base::TRIM_TRAILING, &speed);
      GetDevice(key)->SetSpeed(ConvertToUsbSpeed(speed));
    }

    std::string device_class;
    if (base::ReadFileToString(path.Append("bDeviceClass"), &device_class)) {
      base::TrimWhitespaceASCII(device_class, base::TRIM_TRAILING,
                                &device_class);
      GetDevice(key)->SetDeviceClass(ConvertToUsbClass(device_class));
    }

    std::string version;
    if (base::ReadFileToString(path.Append("version"), &version)) {
      base::TrimWhitespaceASCII(version, base::TRIM_ALL, &version);
      GetDevice(key)->SetVersion(ConvertToUsbVersion(version));
    }

    ReportMetrics(path, key);

  } else {
    if (it == devices_.end()) {
      LOG(WARNING) << "Attempting to remove a non-existent usb device in "
                   << path;
      return;
    }

    devices_.erase(key);
  }
}

UsbDevice* UsbMonitor::GetDevice(std::string key) {
  auto it = devices_.find(key);
  if (it == devices_.end())
    return nullptr;

  return it->second.get();
}

void UsbMonitor::ReportMetrics(const base::FilePath& path, std::string key) {
  if (!metrics_)
    return;

  UsbDevice* device = GetDevice(key);
  if (!device) {
    LOG(WARNING)
        << "Metrics reporting attempted for non-existent usb device in "
        << path;
    return;
  }

  device->ReportMetrics(metrics_);
}

}  // namespace typecd
