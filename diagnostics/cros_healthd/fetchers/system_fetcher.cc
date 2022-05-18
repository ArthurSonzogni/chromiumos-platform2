// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/fetchers/system_fetcher.h"

#include <string>
#include <utility>

#include <base/check.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/strings/stringprintf.h>
#include <base/system/sys_info.h>

#include "diagnostics/cros_healthd/fetchers/system_fetcher_constants.h"
#include "diagnostics/cros_healthd/utils/error_utils.h"
#include "diagnostics/cros_healthd/utils/file_utils.h"

namespace diagnostics {

namespace {

// Fetches information from DMI. Since there are several devices that do not
// provide DMI information, these fields are optional in SystemInfo. As a
// result, a missing DMI file does not indicate a ProbeError. A ProbeError is
// reported when the "chassis_type" field cannot be successfully parsed into an
// unsigned integer.
bool FetchDmiInfo(const base::FilePath& root_dir,
                  mojom::DmiInfoPtr* out_dmi_info,
                  mojom::ProbeErrorPtr* out_error) {
  const auto& dmi_path = root_dir.Append(kRelativePathDmiInfo);
  // If dmi path doesn't exist, the device doesn't support dmi at all. It is
  // considered as successful.
  if (!base::DirectoryExists(dmi_path)) {
    *out_dmi_info = nullptr;
    return true;
  }

  auto dmi_info = mojom::DmiInfo::New();
  ReadAndTrimString(dmi_path, kFileNameBiosVendor, &dmi_info->bios_vendor);
  ReadAndTrimString(dmi_path, kFileNameBiosVersion, &dmi_info->bios_version);
  ReadAndTrimString(dmi_path, kFileNameBoardName, &dmi_info->board_name);
  ReadAndTrimString(dmi_path, kFileNameBoardVendor, &dmi_info->board_vendor);
  ReadAndTrimString(dmi_path, kFileNameBoardVersion, &dmi_info->board_version);
  ReadAndTrimString(dmi_path, kFileNameChassisVendor,
                    &dmi_info->chassis_vendor);
  ReadAndTrimString(dmi_path, kFileNameProductFamily,
                    &dmi_info->product_family);
  ReadAndTrimString(dmi_path, kFileNameProductName, &dmi_info->product_name);
  ReadAndTrimString(dmi_path, kFileNameProductVersion,
                    &dmi_info->product_version);
  ReadAndTrimString(dmi_path, kFileNameSysVendor, &dmi_info->sys_vendor);

  std::string chassis_type_str;
  if (ReadAndTrimString(dmi_path, kFileNameChassisType, &chassis_type_str)) {
    uint64_t chassis_type;
    if (base::StringToUint64(chassis_type_str, &chassis_type)) {
      dmi_info->chassis_type = mojom::NullableUint64::New(chassis_type);
    } else {
      *out_error = CreateAndLogProbeError(
          mojom::ErrorType::kParseError,
          base::StringPrintf("Failed to convert chassis_type: %s",
                             chassis_type_str.c_str()));
      return false;
    }
  }

  *out_dmi_info = std::move(dmi_info);
  return true;
}

bool FetchCachedVpdInfo(const base::FilePath& root_dir,
                        bool has_sku_number,
                        mojom::VpdInfoPtr* out_vpd_info,
                        mojom::ProbeErrorPtr* out_error) {
  auto vpd_info = mojom::VpdInfo::New();

  const auto ro_path = root_dir.Append(kRelativePathVpdRo);
  ReadAndTrimString(ro_path, kFileNameMfgDate, &vpd_info->mfg_date);
  ReadAndTrimString(ro_path, kFileNameModelName, &vpd_info->model_name);
  ReadAndTrimString(ro_path, kFileNameRegion, &vpd_info->region);
  ReadAndTrimString(ro_path, kFileNameSerialNumber, &vpd_info->serial_number);
  if (has_sku_number &&
      !ReadAndTrimString(ro_path, kFileNameSkuNumber, &vpd_info->sku_number)) {
    *out_error = CreateAndLogProbeError(
        mojom::ErrorType::kFileReadError,
        base::StringPrintf("Unable to read VPD file \"%s\" at path: %s",
                           kFileNameSkuNumber, ro_path.value().c_str()));
    return false;
  }

  const auto rw_path = root_dir.Append(kRelativePathVpdRw);
  ReadAndTrimString(rw_path, kFileNameActivateDate, &vpd_info->activate_date);

  if (!base::DirectoryExists(ro_path) && !base::DirectoryExists(rw_path)) {
    // If both the ro and rw path don't exist, sets the whole vpd_info to
    // nullptr. This indicates that the vpd doesn't exist on this platform. It
    // is considered as successful.
    *out_vpd_info = nullptr;
  } else {
    *out_vpd_info = std::move(vpd_info);
  }
  return true;
}

bool GetLsbReleaseValue(const std::string& field,
                        std::string* out_str,
                        mojom::ProbeErrorPtr* out_error) {
  if (base::SysInfo::GetLsbReleaseValue(field, out_str))
    return true;

  *out_error = CreateAndLogProbeError(
      mojom::ErrorType::kFileReadError,
      base::StringPrintf("Unable to read %s from /etc/lsb-release",
                         field.c_str()));
  return false;
}

bool FetchOsVersion(mojom::OsVersionPtr* out_os_version,
                    mojom::ProbeErrorPtr* out_error) {
  auto os_version = mojom::OsVersion::New();
  if (!GetLsbReleaseValue("CHROMEOS_RELEASE_CHROME_MILESTONE",
                          &os_version->release_milestone, out_error))
    return false;
  if (!GetLsbReleaseValue("CHROMEOS_RELEASE_BUILD_NUMBER",
                          &os_version->build_number, out_error))
    return false;
  if (!GetLsbReleaseValue("CHROMEOS_RELEASE_PATCH_NUMBER",
                          &os_version->patch_number, out_error))
    return false;
  if (!GetLsbReleaseValue("CHROMEOS_RELEASE_TRACK",
                          &os_version->release_channel, out_error))
    return false;
  *out_os_version = std::move(os_version);
  return true;
}

bool IsUEFISecureBoot(const std::string& s) {
  if (s.size() != 5) {
    LOG(ERROR) << "Expected 5 bytes from UEFISecureBoot variable, but got "
               << s.size() << " bytes.";
    return false;
  }
  // The first four bytes are the "attributes" of the variable.
  // The last byte indicates the secure boot state.
  switch (s.back()) {
    case '\x00':
      return false;
    case '\x01':
      return true;
    default:
      LOG(ERROR) << "Unexpected secure boot value: " << (uint32_t)(s.back());
      return false;
  }
}

void HandleSecureBootResponse(SystemFetcher::FetchSystemInfoCallback callback,
                              mojom::SystemInfoPtr system_info,
                              const std::string& content) {
  DCHECK(system_info);

  system_info->os_info->boot_mode = !IsUEFISecureBoot(content)
                                        ? mojom::BootMode::kCrosEfi
                                        : mojom::BootMode::kCrosEfiSecure;

  std::move(callback).Run(
      mojom::SystemResult::NewSystemInfo(std::move(system_info)));
}

}  // namespace

void SystemFetcher::FetchBootMode(mojom::SystemInfoPtr system_info,
                                  const base::FilePath& root_dir,
                                  FetchSystemInfoCallback callback) {
  mojom::BootMode* boot_mode = &system_info->os_info->boot_mode;
  // default unknown if there's no match
  *boot_mode = mojom::BootMode::kUnknown;

  std::string cmdline;
  const auto path = root_dir.Append(kFilePathProcCmdline);
  if (!ReadAndTrimString(path, &cmdline)) {
    std::move(callback).Run(
        mojom::SystemResult::NewSystemInfo(std::move(system_info)));
    return;
  }

  auto tokens = base::SplitString(cmdline, " ", base::TRIM_WHITESPACE,
                                  base::SPLIT_WANT_NONEMPTY);
  for (const auto& token : tokens) {
    if (token == "cros_secure") {
      *boot_mode = mojom::BootMode::kCrosSecure;
      break;
    }
    if (token == "cros_efi") {
      context_->executor()->GetUEFISecureBootContent(
          base::BindOnce(&HandleSecureBootResponse, std::move(callback),
                         std::move(system_info)));
      return;
    }
    if (token == "cros_legacy") {
      *boot_mode = mojom::BootMode::kCrosLegacy;
      break;
    }
  }

  std::move(callback).Run(
      mojom::SystemResult::NewSystemInfo(std::move(system_info)));
}

bool SystemFetcher::FetchOsInfoWithoutBootMode(
    mojom::OsInfoPtr* out_os_info, mojom::ProbeErrorPtr* out_error) {
  auto os_info = mojom::OsInfo::New();
  os_info->code_name = context_->system_config()->GetCodeName();
  os_info->marketing_name = context_->system_config()->GetMarketingName();
  os_info->oem_name = context_->system_config()->GetOemName();
  if (!FetchOsVersion(&os_info->os_version, out_error))
    return false;
  *out_os_info = std::move(os_info);
  return true;
}

void SystemFetcher::FetchSystemInfo(FetchSystemInfoCallback callback) {
  const auto& root_dir = context_->root_dir();
  mojom::ProbeErrorPtr error;
  auto system_info = mojom::SystemInfo::New();

  auto& vpd_info = system_info->vpd_info;
  auto& dmi_info = system_info->dmi_info;
  auto& os_info = system_info->os_info;
  if (!FetchCachedVpdInfo(root_dir, context_->system_config()->HasSkuNumber(),
                          &vpd_info, &error) ||
      !FetchDmiInfo(root_dir, &dmi_info, &error) ||
      !FetchOsInfoWithoutBootMode(&os_info, &error)) {
    std::move(callback).Run(mojom::SystemResult::NewError(std::move(error)));
    return;
  }

  // os_info.boot_mode requires ipc with executor, handle separately
  FetchBootMode(std::move(system_info), context_->root_dir(),
                std::move(callback));
}

}  // namespace diagnostics
