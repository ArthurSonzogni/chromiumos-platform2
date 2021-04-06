// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MODEMFWD_MODEM_HELPER_H_
#define MODEMFWD_MODEM_HELPER_H_

#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include <base/files/file_path.h>

namespace modemfwd {

struct FirmwareInfo {
  FirmwareInfo() = default;
  FirmwareInfo(const std::string& main_version,
               const std::string& oem_version,
               const std::string& carrier_uuid,
               const std::string& carrier_version)
      : main_version(main_version),
        oem_version(oem_version),
        carrier_uuid(carrier_uuid),
        carrier_version(carrier_version) {}

  std::string main_version;
  std::string oem_version;
  std::string carrier_uuid;
  std::string carrier_version;
};

struct HelperInfo {
  explicit HelperInfo(const base::FilePath& executable_path)
      : executable_path(executable_path) {}

  base::FilePath executable_path;
  std::vector<std::string> extra_arguments;
};

struct FirmwareConfig {
  std::string fw_type;
  base::FilePath path;
  std::string version;

  // Used to get a proper default matcher in googletest.
  bool operator==(const FirmwareConfig& rhs) const {
    return fw_type == rhs.fw_type && path == rhs.path && version == rhs.version;
  }
};

class ModemHelper {
 public:
  virtual ~ModemHelper() = default;

  virtual bool GetFirmwareInfo(FirmwareInfo* out_info) = 0;

  virtual bool FlashFirmwares(const std::vector<FirmwareConfig>& configs) = 0;

  virtual bool Reboot() = 0;
  virtual bool FlashModeCheck() = 0;
  virtual bool ClearAttachAPN(const std::string& carrier_uuid) = 0;
};

std::unique_ptr<ModemHelper> CreateModemHelper(const HelperInfo& helper_info);

}  // namespace modemfwd

#endif  // MODEMFWD_MODEM_HELPER_H_
