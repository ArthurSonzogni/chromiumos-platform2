// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "typecd/usb_monitor.h"

#include <utility>

#include <base/files/file_util.h>
#include <base/logging.h>
#include <re2/re2.h>

namespace {
constexpr char kTypecPortUeventRegex[] = R"(TYPEC_PORT=port(\d+))";
}

namespace typecd {

void UsbMonitor::OnDeviceAddedOrRemoved(const base::FilePath& path,
                                        bool added) {
  auto key = path.BaseName().value();
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

    devices_.emplace(
        key, std::make_unique<UsbDevice>(std::stoi(busnum), std::stoi(devnum)));

    std::string typec_port_uevent;
    int typec_port_num;
    if (base::ReadFileToString(path.Append("port/connector/uevent"),
                               &typec_port_uevent) &&
        RE2::PartialMatch(typec_port_uevent, kTypecPortUeventRegex,
                          &typec_port_num))
      GetDevice(key)->SetTypecPortNum(typec_port_num);

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

}  // namespace typecd
