// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_FETCHERS_SYSTEM_FETCHER_CONSTANTS_H_
#define DIAGNOSTICS_CROS_HEALTHD_FETCHERS_SYSTEM_FETCHER_CONSTANTS_H_

namespace diagnostics {

// Relative paths to cached VPD information.
inline constexpr char kRelativePathVpdRo[] = "sys/firmware/vpd/ro/";
inline constexpr char kRelativePathVpdRw[] = "sys/firmware/vpd/rw/";

// Files related to cached VPD information.
inline constexpr char kFileNameActivateDate[] = "ActivateDate";
inline constexpr char kFileNameMfgDate[] = "mfg_date";
inline constexpr char kFileNameModelName[] = "model_name";
inline constexpr char kFileNameRegion[] = "region";
inline constexpr char kFileNameSerialNumber[] = "serial_number";
inline constexpr char kFileNameSkuNumber[] = "sku_number";

// Relative path to DMI information.
inline constexpr char kRelativePathDmiInfo[] = "sys/class/dmi/id";

// Files related to DMI information.
inline constexpr char kFileNameBiosVendor[] = "bios_vendor";
inline constexpr char kFileNameBiosVersion[] = "bios_version";
inline constexpr char kFileNameBoardName[] = "board_name";
inline constexpr char kFileNameBoardVendor[] = "board_vendor";
inline constexpr char kFileNameBoardVersion[] = "board_version";
inline constexpr char kFileNameChassisType[] = "chassis_type";
inline constexpr char kFileNameChassisVendor[] = "chassis_vendor";
inline constexpr char kFileNameProductFamily[] = "product_family";
inline constexpr char kFileNameProductName[] = "product_name";
inline constexpr char kFileNameProductVersion[] = "product_version";
inline constexpr char kFileNameSysVendor[] = "sys_vendor";

// Files for boot mode information.
inline constexpr char kFilePathProcCmdline[] = "proc/cmdline";
// Files of EFI global namespace (EFI_GLOBAL_VARIABLE, fixed UUID
// 8be4df61-93ca-11d2-aa0d-00e098032b8c)
// See also:
// https://uefi.org/sites/default/files/resources/UEFI_Spec_2_9_2021_03_18.pdf
inline constexpr char kFileUEFISecurityBoot[] =
    "sys/firmware/efi/vars/SecureBoot-8be4df61-93ca-11d2-aa0d-00e098032b8c/"
    "data";

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_FETCHERS_SYSTEM_FETCHER_CONSTANTS_H_
