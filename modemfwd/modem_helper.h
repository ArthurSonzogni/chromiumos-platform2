// Copyright 2017 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MODEMFWD_MODEM_HELPER_H_
#define MODEMFWD_MODEM_HELPER_H_

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

#include <base/files/file_path.h>
#include <base/memory/scoped_refptr.h>
#include <base/time/time.h>
#include <dbus/bus.h>

namespace modemfwd {

struct FirmwareInfo {
  FirmwareInfo() = default;
  FirmwareInfo(const std::string& main_version,
               const std::string& oem_version,
               const std::string& carrier_uuid,
               const std::string& carrier_version,
               const std::map<std::string, std::string> assoc_versions)
      : main_version(main_version),
        oem_version(oem_version),
        carrier_uuid(carrier_uuid),
        carrier_version(carrier_version),
        assoc_versions(assoc_versions) {}

  std::string main_version;
  std::string oem_version;
  std::string carrier_uuid;
  std::string carrier_version;

  // Additional firmware payloads stored with
  // Tag -> Version mapping
  std::map<std::string, std::string> assoc_versions;
};

struct HelperInfo {
  explicit HelperInfo(const base::FilePath& executable_path)
      : executable_path(executable_path) {}

  base::FilePath executable_path;
  std::vector<std::string> extra_arguments;
  bool net_admin_required;
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

struct HeartbeatConfig {
  int max_failures;
  base::TimeDelta interval;
  // Use longer interval when modem is idle.
  base::TimeDelta modem_idle_interval{base::Seconds(0)};
};

class ModemHelper {
 public:
  virtual ~ModemHelper() = default;

  virtual bool GetFirmwareInfo(FirmwareInfo* out_info,
                               const std::string& firmware_revision) = 0;

  virtual bool FlashFirmwares(const std::vector<FirmwareConfig>& configs) = 0;

  virtual bool Reboot() = 0;
  virtual bool FlashModeCheck() = 0;

  virtual std::optional<HeartbeatConfig> GetHeartbeatConfig() = 0;

  virtual std::vector<base::FilePath> GetRecoveryFileList(
      const base::FilePath& metadata_directory) = 0;
  virtual bool PowerOn() = 0;
  virtual bool PowerOff() = 0;
};

std::unique_ptr<ModemHelper> CreateModemHelper(const HelperInfo& helper_info);

}  // namespace modemfwd

#endif  // MODEMFWD_MODEM_HELPER_H_
