// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef STORAGE_INFO_STORAGE_CAPABILITY_REPORTER_H_
#define STORAGE_INFO_STORAGE_CAPABILITY_REPORTER_H_

#include <inttypes.h>
#include <vector>

#include <base/files/file_path.h>

enum StorageCapabilities {
  STORAGE_PRESENT = 0,
  // eMMC: 1000 - 1999
  MMC_SEC_ERASE_SUPPORTED = 1011,
  MMC_SEC_ERASE_NOT_SUPPORTED = 1012,
  MMC_TRIM_SUPPORTED = 1021,
  MMC_TRIM_NOT_SUPPORTED = 1022,
  MMC_SANITIZE_SUPPORTED = 1031,
  MMC_SANITIZE_NOT_SUPPORTED = 1032,
  MMC_ERASE_CONT_ONE = 1041,
  MMC_ERASE_CONT_ZERO = 1042,
  // NVMe: 2000 - 2999
  NVME_APST_SUPPORTED = 2011,
  NVME_APST_NOT_SUPPORTED = 2012,
  NVME_DEALOC_WZ_SUPPORTED = 2021,
  NVME_DEALOC_WZ_NOT_SUPPORTED = 2022,
  NVME_DEALOC_BYTE_FF = 2031,
  NVME_DEALOC_BYTE_00 = 2032,
  NVME_DEALOC_BYTE_NA = 2033,
  NVME_DEALOC_BYTE_INVAL = 2034,
};

std::vector<StorageCapabilities> CollectEmmcCaps(const std::vector<uint8_t>&);
std::vector<StorageCapabilities> CollectNvmeCaps(const std::vector<uint8_t>&,
                                                 const std::vector<uint8_t>&);
std::vector<StorageCapabilities> CollectUfsCaps(const base::FilePath&);
std::vector<StorageCapabilities> CollectUnknownDevCaps(const base::FilePath&);

std::vector<StorageCapabilities> CollectCaps(const base::FilePath&);
bool ReportCaps(const std::vector<StorageCapabilities>&);

#endif  // STORAGE_INFO_STORAGE_CAPABILITY_REPORTER_H_
