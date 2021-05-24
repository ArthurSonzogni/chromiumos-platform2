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
#include <base/strings/stringprintf.h>
#include <base/strings/string_number_conversions.h>
#include <base/system/sys_info.h>

#include "diagnostics/cros_healthd/fetchers/system_fetcher_constants.h"
#include "diagnostics/cros_healthd/utils/error_utils.h"
#include "diagnostics/cros_healthd/utils/file_utils.h"

namespace diagnostics {

namespace {

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
  ReadAndTrimString(dmi_path, kFileNameBoardVendor, &dmi_info->board_vender);
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

}  // namespace

void SystemFetcher::FetchMasterConfigInfo(mojo_ipc::SystemInfo* output_info) {
  output_info->marketing_name =
      context_->system_config()->GetMarketingName().value_or("");
  output_info->product_name = context_->system_config()->GetCodeName();
}

base::Optional<mojo_ipc::ProbeErrorPtr> SystemFetcher::FetchOsVersion(
    mojo_ipc::OsVersion* os_version) {
  std::string milestone;
  std::string build;
  std::string patch;
  std::string release_channel;

  if (!base::SysInfo::GetLsbReleaseValue("CHROMEOS_RELEASE_CHROME_MILESTONE",
                                         &milestone)) {
    return CreateAndLogProbeError(
        mojo_ipc::ErrorType::kFileReadError,
        "Unable to read OS milestone from /etc/lsb-release");
  }

  if (!base::SysInfo::GetLsbReleaseValue("CHROMEOS_RELEASE_BUILD_NUMBER",
                                         &build)) {
    return CreateAndLogProbeError(
        mojo_ipc::ErrorType::kFileReadError,
        "Unable to read OS build number from /etc/lsb-release");
  }

  if (!base::SysInfo::GetLsbReleaseValue("CHROMEOS_RELEASE_PATCH_NUMBER",
                                         &patch)) {
    return CreateAndLogProbeError(
        mojo_ipc::ErrorType::kFileReadError,
        "Unable to read OS patch number from /etc/lsb-release");
  }

  if (!base::SysInfo::GetLsbReleaseValue("CHROMEOS_RELEASE_TRACK",
                                         &release_channel)) {
    return CreateAndLogProbeError(
        mojo_ipc::ErrorType::kFileReadError,
        "Unable to read OS release track from /etc/lsb-release");
  }

  os_version->release_milestone = milestone;
  os_version->build_number = build;
  os_version->patch_number = patch;
  os_version->release_channel = release_channel;
  return base::nullopt;
}

mojo_ipc::SystemResultPtr SystemFetcher::FetchSystemInfo() {
  const auto& root_dir = context_->root_dir();
  auto system_info = mojo_ipc::SystemInfo::New();
  mojo_ipc::ProbeErrorPtr error;

  mojo_ipc::VpdInfoPtr vpd_info;
  mojo_ipc::DmiInfoPtr dmi_info;
  if (!FetchCachedVpdInfo(root_dir, context_->system_config()->HasSkuNumber(),
                          &vpd_info, &error) ||
      !FetchDmiInfo(root_dir, &dmi_info, &error)) {
    return mojo_ipc::SystemResult::NewError(std::move(error));
  }

  if (vpd_info) {
    system_info->first_power_date = vpd_info->activate_date;
    system_info->manufacture_date = vpd_info->mfg_date;
    system_info->product_sku_number = vpd_info->sku_number;
    system_info->product_serial_number = vpd_info->serial_number;
    system_info->product_model_name = vpd_info->model_name;
  }
  if (dmi_info) {
    system_info->bios_version = dmi_info->bios_version;
    system_info->board_name = dmi_info->board_name;
    system_info->board_version = dmi_info->board_version;
    system_info->chassis_type = dmi_info->chassis_type.Clone();
  }

  base::Optional<mojo_ipc::ProbeErrorPtr> error_opt;

  FetchMasterConfigInfo(system_info.get());

  system_info->os_version = mojo_ipc::OsVersion::New();
  error_opt = FetchOsVersion(system_info->os_version.get());
  if (error_opt.has_value()) {
    return mojo_ipc::SystemResult::NewError(std::move(error_opt.value()));
  }

  return mojo_ipc::SystemResult::NewSystemInfo(std::move(system_info));
}

}  // namespace diagnostics
