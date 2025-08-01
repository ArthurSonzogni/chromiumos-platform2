// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tpm_manager/server/tpm_allowlist_impl.h"

#include <cstring>
#include <optional>
#include <string>
#include <vector>

#include <base/check.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <libhwsec-foundation/tpm/tpm_version.h>

namespace {

#if USE_TPM_DYNAMIC

constexpr char kTpmForceAllowTpmFile[] = "/var/lib/tpm_manager/force_allow_tpm";
constexpr char kAllowedStateFile[] = "/var/lib/tpm_manager/.allowed";
constexpr char kNoPreinitFlagFile[] = "/run/tpm_manager/no_preinit";

// The path to check the TPM is enabled or not.
constexpr char kTpmEnabledFile[] = "/sys/class/tpm/tpm0/enabled";

// The path to check the TPM support sha256 PCR or not.
constexpr char kTpmSha256Pcr0File[] = "/sys/class/tpm/tpm0/pcr-sha256/0";

// Simulator Vendor ID ("SIMU").
constexpr uint32_t kVendorIdSimulator = 0x53494d55;
// STMicroelectronics Vendor ID ("STM ").
constexpr uint32_t kVendorIdStm = 0x53544D20;
// Nuvoton Vendor ID ("NTC").
constexpr uint32_t kVendorIdNtc = 0x4e544300;
// Winbond Vendor ID ("WEC").
constexpr uint32_t kVendorIdWinbond = 0x57454300;
// Atmel Vendor ID ("ATML").
constexpr uint32_t kVendorIdAtmel = 0x41544D4C;
// IBM Vendor ID ("IBM ").
constexpr uint32_t kVendorIdIbm = 0x49424d00;
// Infineon Vendor ID ("IFX  ").
constexpr uint32_t kVendorIdIfx = 0x49465800;

// The location of system vendor information.
constexpr char kSysVendorPath[] = "/sys/class/dmi/id/sys_vendor";
// The location of product name information.
constexpr char kProductNamePath[] = "/sys/class/dmi/id/product_name";
// The location of product family information.
constexpr char kProductFamilyPath[] = "/sys/class/dmi/id/product_family";

constexpr uint32_t kTpm1VendorAllowlist[] = {
    kVendorIdAtmel,
    kVendorIdIbm,
    kVendorIdWinbond,
    kVendorIdIfx,
};

struct DeviceFamily {
  const char* sys_vendor;
  const char* product_family;
  uint32_t tpm_vendor_id;
};

struct DeviceName {
  const char* sys_vendor;
  const char* product_name;
  uint32_t tpm_vendor_id;
};

constexpr DeviceFamily kTpm2FamiliesAllowlist[] = {
    DeviceFamily{"LENOVO", "ThinkPad X1 Carbon Gen 8", kVendorIdStm},
    DeviceFamily{"LENOVO", "ThinkPad X1 Carbon Gen 9", kVendorIdStm},
    DeviceFamily{"LENOVO", "ThinkCentre M70q Gen 3", kVendorIdIfx},
};

constexpr DeviceName kTpm2DeviceNameAllowlist[] = {
    DeviceName{"HP", "HP Elite t655 Thin Client", kVendorIdIfx},
    DeviceName{"HP", "HP Elite x360 830 13 inch G10 2-in-1 Notebook PC",
               kVendorIdNtc},
    DeviceName{"HP", "HP EliteBook 640 14 inch G10 Notebook PC", kVendorIdNtc},
    DeviceName{"HP", "HP EliteBook 645 14 inch G10 Notebook PC", kVendorIdNtc},
    DeviceName{"Dell Inc.", "Latitude 7490", kVendorIdWinbond},
    DeviceName{"Dell Inc.", "Latitude 3520", kVendorIdNtc},
    DeviceName{"HP", "HP ProDesk 400 G5 Desktop Mini", kVendorIdIfx},
    DeviceName{"HP", "HP EliteBook 840 G6", kVendorIdIfx},
    DeviceName{"Intel(R) Client Systems", "NUC11TNKv5", kVendorIdIfx},
    DeviceName{"HP", "HP ZBook Firefly 14 G7 Mobile Workstation", kVendorIdIfx},
    DeviceName{"Dell Inc.", "Latitude 5420", kVendorIdStm},
    DeviceName{"HP", "HP EliteBook 840 G8 Notebook PC", kVendorIdIfx},
    DeviceName{"HP", "HP ProDesk 600 G3 SFF", kVendorIdIfx},
    DeviceName{"Dell Inc.", "Latitude 3420", kVendorIdStm},
    DeviceName{"Dell Inc.", "Latitude 3400", kVendorIdStm},
    DeviceName{"HP", "HP ProDesk 400 G6 Desktop Mini PC", kVendorIdIfx},
    DeviceName{"HP", "HP Z2 Tower G4 Workstation", kVendorIdIfx},
    DeviceName{"HP", "HP ZBook Firefly 14 inch G8 Mobile Workstation PC",
               kVendorIdIfx},
};

std::optional<bool> IsTpmFileEnabled() {
  base::FilePath file_path(kTpmEnabledFile);
  std::string file_content;

  if (!base::ReadFileToString(file_path, &file_content)) {
    return {};
  }

  std::string enabled_str;
  base::TrimWhitespaceASCII(file_content, base::TRIM_ALL, &enabled_str);

  int enabled = 0;
  if (!base::StringToInt(enabled_str, &enabled)) {
    LOG(ERROR) << "enabled is not a number";
    return {};
  }
  return static_cast<bool>(enabled);
}

bool IsTpmSha256PcrSupported() {
  base::FilePath file_path(kTpmSha256Pcr0File);
  std::string file_content;

  if (!base::ReadFileToString(file_path, &file_content)) {
    return false;
  }

  std::string pcr_str;
  base::TrimWhitespaceASCII(file_content, base::TRIM_ALL, &pcr_str);

  return !pcr_str.empty();
}

bool GetSysVendor(std::string* sys_vendor) {
  base::FilePath file_path(kSysVendorPath);
  std::string file_content;

  if (!base::ReadFileToString(file_path, &file_content)) {
    return false;
  }

  base::TrimWhitespaceASCII(file_content, base::TRIM_ALL, sys_vendor);
  return true;
}

bool GetProductName(std::string* product_name) {
  base::FilePath file_path(kProductNamePath);
  std::string file_content;

  if (!base::ReadFileToString(file_path, &file_content)) {
    return false;
  }

  base::TrimWhitespaceASCII(file_content, base::TRIM_ALL, product_name);
  return true;
}

bool GetProductFamily(std::string* product_family) {
  base::FilePath file_path(kProductFamilyPath);
  std::string file_content;

  if (!base::ReadFileToString(file_path, &file_content)) {
    return false;
  }

  base::TrimWhitespaceASCII(file_content, base::TRIM_ALL, product_family);
  return true;
}

std::optional<bool> IsForceAllow() {
  base::FilePath file_path(kTpmForceAllowTpmFile);
  std::string file_content;

  if (!base::ReadFileToString(file_path, &file_content)) {
    return {};
  }

  std::string force_allow_str;
  base::TrimWhitespaceASCII(file_content, base::TRIM_ALL, &force_allow_str);

  int force_allow = 0;
  if (!base::StringToInt(force_allow_str, &force_allow)) {
    LOG(ERROR) << "force_allow is not a number";
    return {};
  }
  return static_cast<bool>(force_allow);
}

std::optional<bool> GetPreviousAllowedState() {
  base::FilePath file_path(kAllowedStateFile);
  std::string file_content;

  if (!base::ReadFileToString(file_path, &file_content)) {
    return {};
  }

  std::string allow_state_str;
  base::TrimWhitespaceASCII(file_content, base::TRIM_ALL, &allow_state_str);

  int allow_state = 0;
  if (!base::StringToInt(allow_state_str, &allow_state)) {
    LOG(ERROR) << "allow_state is not a number";
    return {};
  }
  return static_cast<bool>(allow_state);
}

#endif

}  // namespace

namespace tpm_manager {

TpmAllowlistImpl::TpmAllowlistImpl(TpmStatus* tpm_status)
    : tpm_status_(tpm_status) {
  CHECK(tpm_status_);
}

bool TpmAllowlistImpl::IsAllowed() {
#if !USE_TPM_DYNAMIC
  // Allow all kinds of TPM if we are not using runtime TPM selection.
  return true;
#else

  std::optional<bool> force_allow = IsForceAllow();
  if (force_allow.has_value()) {
    return force_allow.value();
  }

  if (USE_OS_INSTALL_SERVICE) {
    if (base::PathExists(base::FilePath(kNoPreinitFlagFile))) {
      // If USE_OS_INSTALL_SERVICE, kNoPreinitFlagFile will be touched in the
      // pre-start phase of tpm_managerd if the OS is running from installer
      // (see check_tpm_preinit_condition.cc). Note that under current scope
      // USE_OS_INSTALL_SERVICE and USE_TPM_DYNAMIC will always have the same
      // value (and only in reven case both flags are true).
      LOG(WARNING) << __func__
                   << ": Disallow TPM when OS running from installer.";
      return false;
    }
  }

  std::optional<bool> previous_allow_state = GetPreviousAllowedState();
  if (previous_allow_state.has_value()) {
    return previous_allow_state.value();
  }

  if (!tpm_status_->IsTpmEnabled()) {
    LOG(WARNING) << __func__ << ": Disallow the disabled TPM.";
    return false;
  }

  TPM_SELECT_BEGIN;

  TPM2_SECTION({
    if (!IsTpmSha256PcrSupported()) {
      LOG(INFO) << "This TPM doesn't support SHA256 PCR.";
      return false;
    }

    uint32_t family;
    uint64_t spec_level;
    uint32_t manufacturer;
    uint32_t tpm_model;
    uint64_t firmware_version;
    std::vector<uint8_t> vendor_specific;
    if (!tpm_status_->GetVersionInfo(&family, &spec_level, &manufacturer,
                                     &tpm_model, &firmware_version,
                                     &vendor_specific)) {
      LOG(ERROR) << __func__ << ": failed to get version info from tpm status.";
      return false;
    }

    // Allow the tpm2-simulator.
    if (manufacturer == kVendorIdSimulator) {
      return true;
    }

    std::string sys_vendor;
    std::string product_name;
    std::string product_family;

    if (!GetSysVendor(&sys_vendor)) {
      LOG(ERROR) << __func__ << ": Failed to get the system vendor.";
      return false;
    }
    if (!GetProductName(&product_name)) {
      LOG(ERROR) << __func__ << ": Failed to get the product name.";
      return false;
    }
    if (!GetProductFamily(&product_family)) {
      LOG(ERROR) << __func__ << ": Failed to get the product family.";
      return false;
    }

    for (const DeviceFamily& match : kTpm2FamiliesAllowlist) {
      if (sys_vendor == match.sys_vendor &&
          product_family == match.product_family &&
          manufacturer == match.tpm_vendor_id) {
        return true;
      }
    }

    for (const DeviceName& match : kTpm2DeviceNameAllowlist) {
      if (sys_vendor == match.sys_vendor &&
          product_name == match.product_name &&
          manufacturer == match.tpm_vendor_id) {
        return true;
      }
    }

    LOG(INFO) << "Not allowed TPM2.0:";
    LOG(INFO) << "  System Vendor: " << sys_vendor;
    LOG(INFO) << "  Product Name: " << product_name;
    LOG(INFO) << "  Product Family: " << product_family;
    LOG(INFO) << "  TPM Manufacturer: " << std::hex << manufacturer;

    return false;
  });

  TPM1_SECTION({
    std::optional<bool> is_enabled = IsTpmFileEnabled();
    if (is_enabled.has_value() && !is_enabled.value()) {
      LOG(WARNING) << __func__ << ": Disallow the disabled TPM.";
      return false;
    }

    uint32_t family;
    uint64_t spec_level;
    uint32_t manufacturer;
    uint32_t tpm_model;
    uint64_t firmware_version;
    std::vector<uint8_t> vendor_specific;
    if (!tpm_status_->GetVersionInfo(&family, &spec_level, &manufacturer,
                                     &tpm_model, &firmware_version,
                                     &vendor_specific)) {
      LOG(ERROR) << __func__ << ": failed to get version info from tpm status.";
      return false;
    }

    for (uint32_t vendor_id : kTpm1VendorAllowlist) {
      if (manufacturer == vendor_id) {
        return true;
      }
    }

    LOG(INFO) << "Not allowed TPM1.2:";
    LOG(INFO) << "  TPM Manufacturer: " << std::hex << manufacturer;

    return false;
  });

  OTHER_TPM_SECTION({
    // We don't allow the other TPM cases.
    return false;
  });

  TPM_SELECT_END;
#endif
}

}  // namespace tpm_manager
