// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "storage_info/storage_capability_reporter.h"

#include <inttypes.h>
#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/stringprintf.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <brillo/blkdev_utils/storage_utils.h>
#include <brillo/process/process.h>
#include <metrics/metrics_library.h>

namespace {
std::vector<uint8_t> ReadExtcsd(const base::FilePath& rootdev) {
  std::vector<uint8_t> extcsd;
  std::string extcsd_str;
  std::string rootdev_str = rootdev.value();

  if (rootdev_str.empty()) {
    LOG(ERROR) << "Malformed rootdev: " << rootdev;
    return {};
  }

  char id = rootdev_str[rootdev_str.length() - 1];
  base::FilePath debugfs(
      base::StringPrintf("/sys/kernel/debug/mmc%c/mmc%c:0001/ext_csd", id, id));
  if (!base::ReadFileToString(debugfs, &extcsd_str)) {
    PLOG(ERROR) << "Can not read ext_csd";
    return {};
  }
  base::TrimWhitespaceASCII(extcsd_str, base::TRIM_ALL, &extcsd_str);
  if (!base::HexStringToBytes(extcsd_str, &extcsd)) {
    LOG(ERROR) << "Can not convert hex string: " << extcsd_str;
    return {};
  }

  return extcsd;
}

std::vector<uint8_t> ReadIdCtrl(const base::FilePath& rootdev) {
  brillo::ProcessImpl proc;
  proc.AddArg("/usr/sbin/nvme");
  proc.AddArg("id-ctrl");
  proc.AddArg("-b");
  proc.AddArg(rootdev.value());
  proc.RedirectOutputToMemory(false);
  int status = proc.Run();
  if (status != 0) {
    LOG(ERROR) << "Failed to run nvme cli: "
               << proc.GetOutputString(STDERR_FILENO);
    return {};
  }

  std::string result = proc.GetOutputString(STDOUT_FILENO);

  return std::vector<uint8_t>(result.begin(), result.end());
}

std::vector<uint8_t> ReadIdNs(const base::FilePath& rootdev) {
  brillo::ProcessImpl proc;
  proc.AddArg("/usr/sbin/nvme");
  proc.AddArg("id-ns");
  proc.AddArg("-b");
  proc.AddArg(rootdev.value());
  proc.RedirectOutputToMemory(false);
  int status = proc.Run();
  if (status != 0) {
    LOG(ERROR) << "Failed to run nvme cli: "
               << proc.GetOutputString(STDERR_FILENO);
    return {};
  }

  std::string result = proc.GetOutputString(STDOUT_FILENO);

  return std::vector<uint8_t>(result.begin(), result.end());
}

int GetBit(const std::vector<uint8_t>& extcsd, int byte, int bit) {
  return (extcsd.at(byte) >> bit) & 1;
}

}  // namespace

std::vector<StorageCapabilities> CollectEmmcCaps(
    const std::vector<uint8_t>& extcsd) {
  if (extcsd.empty()) {
    return {};
  }

  return std::vector<StorageCapabilities>{
      StorageCapabilities::STORAGE_PRESENT,
      GetBit(extcsd, 231, 0) ? StorageCapabilities::MMC_SEC_ERASE_SUPPORTED
                             : StorageCapabilities::MMC_SEC_ERASE_NOT_SUPPORTED,
      GetBit(extcsd, 231, 4) ? StorageCapabilities::MMC_TRIM_SUPPORTED
                             : StorageCapabilities::MMC_TRIM_NOT_SUPPORTED,
      GetBit(extcsd, 231, 6) ? StorageCapabilities::MMC_SANITIZE_SUPPORTED
                             : StorageCapabilities::MMC_SANITIZE_NOT_SUPPORTED,
      GetBit(extcsd, 181, 0) ? StorageCapabilities::MMC_ERASE_CONT_ONE
                             : StorageCapabilities::MMC_ERASE_CONT_ZERO};
}

std::vector<StorageCapabilities> CollectNvmeCaps(
    const std::vector<uint8_t>& idctrl, const std::vector<uint8_t>& idns) {
  if (idctrl.empty() || idns.empty()) {
    return {};
  }

  std::vector<StorageCapabilities> caps{
      StorageCapabilities::STORAGE_PRESENT,
      GetBit(idctrl, 265, 0) ? StorageCapabilities::NVME_APST_SUPPORTED
                             : StorageCapabilities::NVME_APST_NOT_SUPPORTED,
      GetBit(idns, 33, 3) ? StorageCapabilities::NVME_DEALOC_WZ_SUPPORTED
                          : StorageCapabilities::NVME_DEALOC_WZ_NOT_SUPPORTED};

  int dealoc_byte = (GetBit(idns, 33, 2) << 2) + (GetBit(idns, 33, 1) << 1) +
                    GetBit(idns, 33, 0);

  switch (dealoc_byte) {
    case 0:
      caps.push_back(StorageCapabilities::NVME_DEALOC_BYTE_NA);
      break;
    case 1:
      caps.push_back(StorageCapabilities::NVME_DEALOC_BYTE_00);
      break;
    case 2:
      caps.push_back(StorageCapabilities::NVME_DEALOC_BYTE_FF);
      break;
    default:
      caps.push_back(StorageCapabilities::NVME_DEALOC_BYTE_INVAL);
  }

  return caps;
}

std::vector<StorageCapabilities> CollectUfsCaps(const base::FilePath& rootdev) {
  if (rootdev.empty()) {
    return {};
  }
  return {StorageCapabilities::STORAGE_PRESENT};
}

std::vector<StorageCapabilities> CollectUnknownDevCaps(
    const base::FilePath& rootdev) {
  LOG(INFO) << "No capabilities are collected for the device: " << rootdev;
  return {};
}

std::vector<StorageCapabilities> CollectCaps(const base::FilePath& rootdev) {
  switch (brillo::StorageUtils().GetStorageType(base::FilePath("/"), rootdev)) {
    case brillo::StorageType::emmc:
      return CollectEmmcCaps(ReadExtcsd(rootdev));
    case brillo::StorageType::nvme:
      return CollectNvmeCaps(ReadIdCtrl(rootdev), ReadIdNs(rootdev));
    case brillo::StorageType::ufs:
      return CollectUfsCaps(rootdev);
    default:
      return CollectUnknownDevCaps(rootdev);
  }
}

bool ReportCaps(const std::vector<StorageCapabilities>& caps) {
  if (caps.empty()) {
    return false;
  }

  bool success = true;
  MetricsLibrary lib;
  for (auto c : caps) {
    LOG(INFO) << "Sending capability to UMA: " << c;
    success &= lib.SendSparseToUMA("Platform.StorageCapabilities", c);
  }

  return success;
}
