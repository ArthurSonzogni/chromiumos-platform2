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
#include <base/optional.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/strings/stringprintf.h>
#include <base/system/sys_info.h>

#include "diagnostics/cros_healthd/fetchers/system_fetcher_constants.h"
#include "diagnostics/cros_healthd/utils/error_utils.h"
#include "diagnostics/cros_healthd/utils/file_utils.h"

namespace diagnostics {

namespace {

namespace executor_ipc = chromeos::cros_healthd_executor::mojom;
namespace mojo_ipc = ::chromeos::cros_healthd::mojom;

// Fetches information from DMI. Since there are several devices that do not
// provide DMI information, these fields are optional in SystemInfo. As a
// result, a missing DMI file does not indicate a ProbeError. A ProbeError is
// reported when the "chassis_type" field cannot be successfully parsed into an
// unsigned integer.
bool FetchDmiInfo(const base::FilePath& root_dir,
                  mojo_ipc::DmiInfoPtr* out_dmi_info,
                  mojo_ipc::ProbeErrorPtr* out_error) {
  const auto& dmi_path = root_dir.Append(kRelativePathDmiInfo);
  // If dmi path doesn't exist, the device doesn't support dmi at all. It is
  // considered as successful.
  if (!base::DirectoryExists(dmi_path)) {
    *out_dmi_info = nullptr;
    return true;
  }

  auto dmi_info = mojo_ipc::DmiInfo::New();
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
      dmi_info->chassis_type = mojo_ipc::NullableUint64::New(chassis_type);
    } else {
      *out_error = CreateAndLogProbeError(
          mojo_ipc::ErrorType::kParseError,
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
                        mojo_ipc::VpdInfoPtr* out_vpd_info,
                        mojo_ipc::ProbeErrorPtr* out_error) {
  auto vpd_info = mojo_ipc::VpdInfo::New();

  const auto ro_path = root_dir.Append(kRelativePathVpdRo);
  ReadAndTrimString(ro_path, kFileNameMfgDate, &vpd_info->mfg_date);
  ReadAndTrimString(ro_path, kFileNameModelName, &vpd_info->model_name);
  ReadAndTrimString(ro_path, kFileNameRegion, &vpd_info->region);
  ReadAndTrimString(ro_path, kFileNameSerialNumber, &vpd_info->serial_number);
  if (has_sku_number &&
      !ReadAndTrimString(ro_path, kFileNameSkuNumber, &vpd_info->sku_number)) {
    *out_error = CreateAndLogProbeError(
        mojo_ipc::ErrorType::kFileReadError,
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
                        mojo_ipc::ProbeErrorPtr* out_error) {
  if (base::SysInfo::GetLsbReleaseValue(field, out_str))
    return true;

  *out_error = CreateAndLogProbeError(
      mojo_ipc::ErrorType::kFileReadError,
      base::StringPrintf("Unable to read %s from /etc/lsb-release",
                         field.c_str()));
  return false;
}

bool FetchOsVersion(mojo_ipc::OsVersionPtr* out_os_version,
                    mojo_ipc::ProbeErrorPtr* out_error) {
  auto os_version = mojo_ipc::OsVersion::New();
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
  if (s.size() != 1) {
    LOG(ERROR) << "Expected 1 byte from UEFISecureBoot variable, but got "
               << s.size() << " bytes.";
    return false;
  }
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

void HandleSecureBootResponse(SystemFetcher::FetchSystemInfoV2Callback callback,
                              mojo_ipc::SystemInfoV2Ptr system_info_v2,
                              const std::string& content) {
  DCHECK(system_info_v2);

  system_info_v2->os_info->boot_mode = !IsUEFISecureBoot(content)
                                           ? mojo_ipc::BootMode::kCrosEfi
                                           : mojo_ipc::BootMode::kCrosEfiSecure;

  std::move(callback).Run(
      mojo_ipc::SystemResultV2::NewSystemInfoV2(std::move(system_info_v2)));
}

void HandleSystemInfoV2Response(SystemFetcher::FetchSystemInfoCallback callback,
                                mojo_ipc::SystemResultV2Ptr result) {
  if (result->is_error()) {
    std::move(callback).Run(
        mojo_ipc::SystemResult::NewError(result->get_error()->Clone()));
    return;
  }
  CHECK(result->is_system_info_v2());
  auto system_info =
      SystemFetcher::ConvertToSystemInfo(result->get_system_info_v2());
  std::move(callback).Run(
      mojo_ipc::SystemResult::NewSystemInfo(std::move(system_info)));
}

}  // namespace

void SystemFetcher::FetchBootMode(mojo_ipc::SystemInfoV2Ptr system_info_v2,
                                  const base::FilePath& root_dir,
                                  FetchSystemInfoV2Callback callback) {
  mojo_ipc::BootMode* boot_mode = &system_info_v2->os_info->boot_mode;
  // default unknown if there's no match
  *boot_mode = mojo_ipc::BootMode::kUnknown;

  std::string cmdline;
  const auto path = root_dir.Append(kFilePathProcCmdline);
  if (!ReadAndTrimString(path, &cmdline)) {
    std::move(callback).Run(
        mojo_ipc::SystemResultV2::NewSystemInfoV2(std::move(system_info_v2)));
    return;
  }

  auto tokens = base::SplitString(cmdline, " ", base::TRIM_WHITESPACE,
                                  base::SPLIT_WANT_NONEMPTY);
  for (const auto& token : tokens) {
    if (token == "cros_secure") {
      *boot_mode = mojo_ipc::BootMode::kCrosSecure;
      break;
    }
    if (token == "cros_efi") {
      context_->executor()->GetUEFISecureBootContent(
          base::BindOnce(&HandleSecureBootResponse, std::move(callback),
                         std::move(system_info_v2)));
      return;
    }
    if (token == "cros_legacy") {
      *boot_mode = mojo_ipc::BootMode::kCrosLegacy;
      break;
    }
  }

  std::move(callback).Run(
      mojo_ipc::SystemResultV2::NewSystemInfoV2(std::move(system_info_v2)));
}

bool SystemFetcher::FetchOsInfoWithoutBootMode(
    mojo_ipc::OsInfoPtr* out_os_info, mojo_ipc::ProbeErrorPtr* out_error) {
  auto os_info = mojo_ipc::OsInfo::New();
  os_info->code_name = context_->system_config()->GetCodeName();
  os_info->marketing_name = context_->system_config()->GetMarketingName();
  if (!FetchOsVersion(&os_info->os_version, out_error))
    return false;
  *out_os_info = std::move(os_info);
  return true;
}

void SystemFetcher::FetchSystemInfoV2(FetchSystemInfoV2Callback callback) {
  const auto& root_dir = context_->root_dir();
  mojo_ipc::ProbeErrorPtr error;
  auto system_info_v2 = mojo_ipc::SystemInfoV2::New();

  auto& vpd_info = system_info_v2->vpd_info;
  auto& dmi_info = system_info_v2->dmi_info;
  auto& os_info = system_info_v2->os_info;
  if (!FetchCachedVpdInfo(root_dir, context_->system_config()->HasSkuNumber(),
                          &vpd_info, &error) ||
      !FetchDmiInfo(root_dir, &dmi_info, &error) ||
      !FetchOsInfoWithoutBootMode(&os_info, &error)) {
    std::move(callback).Run(
        mojo_ipc::SystemResultV2::NewError(std::move(error)));
    return;
  }

  // os_info.boot_mode requires ipc with executor, handle separately
  FetchBootMode(std::move(system_info_v2), context_->root_dir(),
                std::move(callback));
}

mojo_ipc::SystemInfoPtr SystemFetcher::ConvertToSystemInfo(
    const mojo_ipc::SystemInfoV2Ptr& system_info_v2) {
  if (system_info_v2.is_null())
    return nullptr;

  auto system_info = mojo_ipc::SystemInfo::New();

  const auto& vpd_info = system_info_v2->vpd_info;
  if (vpd_info) {
    system_info->first_power_date = vpd_info->activate_date;
    system_info->manufacture_date = vpd_info->mfg_date;
    system_info->product_sku_number = vpd_info->sku_number;
    system_info->product_serial_number = vpd_info->serial_number;
    system_info->product_model_name = vpd_info->model_name;
  }
  const auto& dmi_info = system_info_v2->dmi_info;
  if (dmi_info) {
    system_info->bios_version = dmi_info->bios_version;
    system_info->board_name = dmi_info->board_name;
    system_info->board_version = dmi_info->board_version;
    system_info->chassis_type = dmi_info->chassis_type.Clone();
  }
  const auto& os_info = system_info_v2->os_info;
  CHECK(os_info);
  system_info->product_name = os_info->code_name;
  // |marketing_name| is an optional field in cros_confg. Set it to null string
  // if it is missed.
  system_info->marketing_name = os_info->marketing_name.value_or("");
  system_info->os_version = os_info->os_version.Clone();

  return system_info;
}

void SystemFetcher::FetchSystemInfo(FetchSystemInfoCallback callback) {
  FetchSystemInfoV2(
      base::BindOnce(&HandleSystemInfoV2Response, std::move(callback)));
}

}  // namespace diagnostics
