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
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>

#include "gmock/gmock.h"

using ::testing::ElementsAre;

namespace {

std::vector<uint8_t> ReadTestData(const std::string& fname) {
  base::FilePath fpath(fname);
  std::string text;
  std::vector<uint8_t> result;
  if (!base::ReadFileToString(fpath, &text)) {
    PLOG(ERROR) << "Can not test file: " << fpath.value();
    return {};
  }
  base::TrimWhitespaceASCII(text, base::TRIM_ALL, &text);
  if (!base::HexStringToBytes(text, &result)) {
    LOG(ERROR) << "Can not convert hex string: " << text;
    return {};
  }

  return result;
}

}  // namespace

TEST(StorageCapabilities, EmmcCaps_NoExtcsd) {
  std::vector<StorageCapabilities> caps = CollectEmmcCaps({});

  ASSERT_THAT(caps, ElementsAre());
}

TEST(StorageCapabilities, EmmcCaps_Set1) {
  std::vector<StorageCapabilities> caps =
      CollectEmmcCaps(ReadTestData("testdata/extcsd1"));

  ASSERT_THAT(caps, ElementsAre(StorageCapabilities::STORAGE_PRESENT,
                                StorageCapabilities::MMC_SEC_ERASE_SUPPORTED,
                                StorageCapabilities::MMC_TRIM_SUPPORTED,
                                StorageCapabilities::MMC_SANITIZE_SUPPORTED,
                                StorageCapabilities::MMC_ERASE_CONT_ZERO));
}

TEST(StorageCapabilities, NvmeCaps_NoIdctrl_NoIdns) {
  std::vector<StorageCapabilities> caps = CollectNvmeCaps({}, {});

  ASSERT_THAT(caps, ElementsAre());
}

TEST(StorageCapabilities, NvmeCaps_NoIdctrl) {
  std::vector<StorageCapabilities> caps = CollectNvmeCaps({}, {1});

  ASSERT_THAT(caps, ElementsAre());
}

TEST(StorageCapabilities, NvmeCaps_NoIdns) {
  std::vector<StorageCapabilities> caps = CollectNvmeCaps({1}, {});

  ASSERT_THAT(caps, ElementsAre());
}

TEST(StorageCapabilities, NvmeCaps_Set1) {
  std::vector<StorageCapabilities> caps = CollectNvmeCaps(
      ReadTestData("testdata/idctrl1"), ReadTestData("testdata/idns1"));

  ASSERT_THAT(caps, ElementsAre(StorageCapabilities::STORAGE_PRESENT,
                                StorageCapabilities::NVME_APST_SUPPORTED,
                                StorageCapabilities::NVME_DEALOC_WZ_SUPPORTED,
                                StorageCapabilities::NVME_DEALOC_BYTE_00));
}

TEST(StorageCapabilities, UfsCaps_EmptyRootdev) {
  std::vector<StorageCapabilities> caps = CollectUfsCaps(base::FilePath());

  ASSERT_THAT(caps, ElementsAre());
}

TEST(StorageCapabilities, UfsCaps_ValidRootdev) {
  std::vector<StorageCapabilities> caps =
      CollectUfsCaps(base::FilePath("/dev/sda"));

  ASSERT_THAT(caps, ElementsAre(StorageCapabilities::STORAGE_PRESENT));
}

TEST(StorageCapabilities, UnknownDevType) {
  std::vector<StorageCapabilities> caps =
      CollectUnknownDevCaps(base::FilePath("/dev/hda"));

  ASSERT_THAT(caps, ElementsAre());
}
