// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/fetchers/storage/device_info.h"

#include <memory>
#include <utility>

#include <base/files/file_path.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "diagnostics/cros_healthd/fetchers/storage/mock/mock_platform.h"
#include "diagnostics/mojom/public/cros_healthd_probe.mojom.h"

namespace diagnostics {
namespace {

namespace mojom = ::ash::cros_healthd::mojom;
using ::testing::ReturnPointee;
using ::testing::StrictMock;

constexpr char kFakeDevnode[] = "dev/node/path";
constexpr char kFakeSubsystemMmc[] = "block:mmc";
constexpr char kFakeSubsystemNvme[] = "block:nvme";
constexpr char kFakeSubsystemUfs[] = "block:scsi:scsi:scsi:pci";
constexpr char kFakeSubsystemSata[] = "block:scsi:pci";
constexpr uint64_t kFakeSize = 16 * 1024;
constexpr uint64_t kFakeBlockSize = 512;
constexpr mojom::StorageDevicePurpose kFakePurpose =
    mojom::StorageDevicePurpose::kSwapDevice;

class StorageDeviceInfoTest : public ::testing::Test {
 protected:
  std::unique_ptr<StrictMock<MockPlatform>> CreateMockPlatform() {
    auto mock_platform = std::make_unique<StrictMock<MockPlatform>>();
    EXPECT_CALL(*mock_platform,
                GetDeviceSizeBytes(base::FilePath(kFakeDevnode)))
        .WillOnce(ReturnPointee(&kFakeSize));
    EXPECT_CALL(*mock_platform,
                GetDeviceBlockSizeBytes(base::FilePath(kFakeDevnode)))
        .WillOnce(ReturnPointee(&kFakeBlockSize));
    return mock_platform;
  }
};

TEST_F(StorageDeviceInfoTest, FetchEmmcTest) {
  constexpr char kPath[] =
      "cros_healthd/fetchers/storage/testdata/sys/block/mmcblk0";
  auto mock_platform = CreateMockPlatform();
  auto dev_info = StorageDeviceInfo::Create(
      base::FilePath(kPath), base::FilePath(kFakeDevnode), kFakeSubsystemMmc,
      kFakePurpose, mock_platform.get());
  EXPECT_NE(dev_info, nullptr);

  auto info_result = dev_info->FetchDeviceInfo();
  EXPECT_TRUE(info_result.has_value());

  auto info = std::move(info_result.value());
  EXPECT_EQ(info->path, kFakeDevnode);
  EXPECT_EQ(info->type, kFakeSubsystemMmc);
  EXPECT_EQ(info->size, kFakeSize);
  EXPECT_EQ(info->read_time_seconds_since_last_boot, 184);
  EXPECT_EQ(info->write_time_seconds_since_last_boot, 13849);
  EXPECT_EQ(info->bytes_read_since_last_boot,
            (uint64_t)84710472 * kFakeBlockSize);
  EXPECT_EQ(info->bytes_written_since_last_boot,
            (uint64_t)7289304 * kFakeBlockSize);
  EXPECT_EQ(info->io_time_seconds_since_last_boot, 7392);
  EXPECT_TRUE(info->discard_time_seconds_since_last_boot.is_null());
  EXPECT_EQ(info->vendor_id->get_emmc_oemid(), 0x5050);
  EXPECT_EQ(info->product_id->get_emmc_pnm(), 0x4D4E504D4E50);
  EXPECT_EQ(info->revision->get_emmc_prv(), 0x8);
  EXPECT_EQ(info->name, "PNMPNM");
  EXPECT_EQ(info->firmware_string, "0x1223344556677889");
  EXPECT_EQ(info->firmware_version->get_emmc_fwrev(), 0x1223344556677889);
  EXPECT_TRUE(info->device_info->is_emmc_device_info());
  EXPECT_EQ(info->device_info->get_emmc_device_info()->manfid, 0xA5);
  EXPECT_EQ(info->device_info->get_emmc_device_info()->pnm, 0x4D4E504D4E50);
  EXPECT_EQ(info->device_info->get_emmc_device_info()->prv, 0x8);
  EXPECT_EQ(info->device_info->get_emmc_device_info()->fwrev,
            0x1223344556677889);
  EXPECT_EQ(info->purpose, kFakePurpose);
  EXPECT_EQ(info->manufacturer_id, 0xA5);
  EXPECT_EQ(info->serial, 0x1EAFBED5);
}

TEST_F(StorageDeviceInfoTest, FetchEmmcTestWithOldMmc) {
  constexpr char kPath[] =
      "cros_healthd/fetchers/storage/testdata/sys/block/mmcblk2";
  auto mock_platform = CreateMockPlatform();
  auto dev_info = StorageDeviceInfo::Create(
      base::FilePath(kPath), base::FilePath(kFakeDevnode), kFakeSubsystemMmc,
      kFakePurpose, mock_platform.get());
  EXPECT_NE(dev_info, nullptr);

  auto info_result = dev_info->FetchDeviceInfo();
  EXPECT_TRUE(info_result.has_value());

  auto info = std::move(info_result.value());
  EXPECT_EQ(info->path, kFakeDevnode);
  EXPECT_EQ(info->type, kFakeSubsystemMmc);
  EXPECT_EQ(info->size, kFakeSize);
  EXPECT_EQ(info->read_time_seconds_since_last_boot, 184);
  EXPECT_EQ(info->write_time_seconds_since_last_boot, 13849);
  EXPECT_EQ(info->bytes_read_since_last_boot,
            (uint64_t)84710472 * kFakeBlockSize);
  EXPECT_EQ(info->bytes_written_since_last_boot,
            (uint64_t)7289304 * kFakeBlockSize);
  EXPECT_EQ(info->io_time_seconds_since_last_boot, 7392);
  EXPECT_TRUE(info->discard_time_seconds_since_last_boot.is_null());
  EXPECT_EQ(info->vendor_id->get_emmc_oemid(), 0x5050);
  EXPECT_EQ(info->product_id->get_emmc_pnm(), 0x4D4E504D4E50);
  EXPECT_EQ(info->revision->get_emmc_prv(), 0x4);
  EXPECT_EQ(info->name, "PNMPNM");
  EXPECT_EQ(info->firmware_string, "0x1223344556677889");
  EXPECT_EQ(info->firmware_version->get_emmc_fwrev(), 0x1223344556677889);
  EXPECT_TRUE(info->device_info->is_emmc_device_info());
  EXPECT_EQ(info->device_info->get_emmc_device_info()->manfid, 0xA5);
  EXPECT_EQ(info->device_info->get_emmc_device_info()->pnm, 0x4D4E504D4E50);
  EXPECT_EQ(info->device_info->get_emmc_device_info()->prv, 0x4);
  EXPECT_EQ(info->device_info->get_emmc_device_info()->fwrev,
            0x1223344556677889);
  EXPECT_EQ(info->purpose, kFakePurpose);
  EXPECT_EQ(info->manufacturer_id, 0xA5);
  EXPECT_EQ(info->serial, 0x1EAFBED5);
}

TEST_F(StorageDeviceInfoTest, FetchEmmcTestWithNoData) {
  constexpr char kPath[] =
      "cros_healthd/fetchers/storage/testdata/sys/block/mmcblk1";
  auto mock_platform = std::make_unique<StrictMock<MockPlatform>>();
  auto dev_info = StorageDeviceInfo::Create(
      base::FilePath(kPath), base::FilePath(kFakeDevnode), kFakeSubsystemMmc,
      kFakePurpose, mock_platform.get());
  EXPECT_EQ(dev_info, nullptr);
}

TEST_F(StorageDeviceInfoTest, FetchNvmeTest) {
  constexpr char kPath[] =
      "cros_healthd/fetchers/storage/testdata/sys/block/nvme0n1";
  auto mock_platform = CreateMockPlatform();
  auto dev_info = StorageDeviceInfo::Create(
      base::FilePath(kPath), base::FilePath(kFakeDevnode), kFakeSubsystemNvme,
      kFakePurpose, mock_platform.get());
  EXPECT_NE(dev_info, nullptr);

  auto info_result = dev_info->FetchDeviceInfo();
  EXPECT_TRUE(info_result.has_value());

  auto info = std::move(info_result.value());
  EXPECT_EQ(info->path, kFakeDevnode);
  EXPECT_EQ(info->type, kFakeSubsystemNvme);
  EXPECT_EQ(info->size, kFakeSize);
  EXPECT_EQ(info->read_time_seconds_since_last_boot, 144);
  EXPECT_EQ(info->write_time_seconds_since_last_boot, 22155);
  EXPECT_EQ(info->bytes_read_since_last_boot,
            (uint64_t)35505772 * kFakeBlockSize);
  EXPECT_EQ(info->bytes_written_since_last_boot,
            (uint64_t)665648234 * kFakeBlockSize);
  EXPECT_EQ(info->io_time_seconds_since_last_boot, 4646);
  EXPECT_EQ(info->discard_time_seconds_since_last_boot->value, 200);
  EXPECT_EQ(info->vendor_id->get_nvme_subsystem_vendor(), 0x1812);
  EXPECT_EQ(info->product_id->get_nvme_subsystem_device(), 0x3243);
  EXPECT_EQ(info->revision->get_nvme_pcie_rev(), 0x13);
  EXPECT_EQ(info->name, "test_nvme_model");
  EXPECT_EQ(info->firmware_string, "TEST_REV");
  EXPECT_EQ(info->firmware_version->get_nvme_firmware_rev(),
            0x5645525F54534554);
  EXPECT_TRUE(info->device_info->is_nvme_device_info());
  EXPECT_EQ(info->device_info->get_nvme_device_info()->subsystem_vendor,
            0x1812);
  EXPECT_EQ(info->device_info->get_nvme_device_info()->subsystem_device,
            0x3243);
  EXPECT_EQ(info->device_info->get_nvme_device_info()->pcie_rev, 0x13);
  EXPECT_EQ(info->device_info->get_nvme_device_info()->firmware_rev,
            0x5645525F54534554);
  EXPECT_EQ(kFakePurpose, info->purpose);
  EXPECT_EQ(info->manufacturer_id, 0);
  EXPECT_EQ(info->serial, 0);
}

TEST_F(StorageDeviceInfoTest, FetchNvmeTestWithLegacyRevision) {
  constexpr char kPath[] =
      "cros_healthd/fetchers/storage/testdata/sys/block/missing_revision";
  auto mock_platform = CreateMockPlatform();
  auto dev_info = StorageDeviceInfo::Create(
      base::FilePath(kPath), base::FilePath(kFakeDevnode), kFakeSubsystemNvme,
      kFakePurpose, mock_platform.get());
  EXPECT_NE(dev_info, nullptr);

  auto info_result = dev_info->FetchDeviceInfo();
  EXPECT_TRUE(info_result.has_value());

  auto info = std::move(info_result.value());
  EXPECT_EQ(info->path, kFakeDevnode);
  EXPECT_EQ(info->type, kFakeSubsystemNvme);
  EXPECT_EQ(info->size, kFakeSize);
  EXPECT_EQ(info->read_time_seconds_since_last_boot, 144);
  EXPECT_EQ(info->write_time_seconds_since_last_boot, 22155);
  EXPECT_EQ(info->bytes_read_since_last_boot,
            (uint64_t)35505772 * kFakeBlockSize);
  EXPECT_EQ(info->bytes_written_since_last_boot,
            (uint64_t)665648234 * kFakeBlockSize);
  EXPECT_EQ(info->io_time_seconds_since_last_boot, 4646);
  EXPECT_EQ(info->discard_time_seconds_since_last_boot->value, 200);
  EXPECT_EQ(info->vendor_id->get_nvme_subsystem_vendor(), 0x1812);
  EXPECT_EQ(info->product_id->get_nvme_subsystem_device(), 0x3243);
  EXPECT_EQ(info->revision->get_nvme_pcie_rev(), 0x17);
  EXPECT_EQ(info->name, "test_nvme_model");
  EXPECT_EQ(info->firmware_string, "TEST_REV");
  EXPECT_EQ(info->firmware_version->get_nvme_firmware_rev(),
            0x5645525F54534554);
  EXPECT_TRUE(info->device_info->is_nvme_device_info());
  EXPECT_EQ(info->device_info->get_nvme_device_info()->subsystem_vendor,
            0x1812);
  EXPECT_EQ(info->device_info->get_nvme_device_info()->subsystem_device,
            0x3243);
  EXPECT_EQ(info->device_info->get_nvme_device_info()->pcie_rev, 0x17);
  EXPECT_EQ(info->device_info->get_nvme_device_info()->firmware_rev,
            0x5645525F54534554);
  EXPECT_EQ(info->purpose, kFakePurpose);
  EXPECT_EQ(info->manufacturer_id, 0);
  EXPECT_EQ(info->serial, 0);
}

TEST_F(StorageDeviceInfoTest, FetchNvmeTestWithNoData) {
  constexpr char kPath[] =
      "cros_healthd/fetchers/storage/testdata/sys/block/nvme0n2";
  auto mock_platform = std::make_unique<StrictMock<MockPlatform>>();
  auto dev_info = StorageDeviceInfo::Create(
      base::FilePath(kPath), base::FilePath(kFakeDevnode), kFakeSubsystemNvme,
      kFakePurpose, mock_platform.get());
  EXPECT_EQ(dev_info, nullptr);
}

TEST_F(StorageDeviceInfoTest, FetchUFSTest) {
  constexpr char kPath[] =
      "cros_healthd/fetchers/storage/testdata/sys/block/sda";
  auto mock_platform = CreateMockPlatform();
  auto dev_info = StorageDeviceInfo::Create(
      base::FilePath(kPath), base::FilePath(kFakeDevnode), kFakeSubsystemUfs,
      kFakePurpose, mock_platform.get());
  EXPECT_NE(dev_info, nullptr);

  auto info_result = dev_info->FetchDeviceInfo();
  EXPECT_TRUE(info_result.has_value());

  auto info = std::move(info_result.value());
  EXPECT_EQ(info->path, kFakeDevnode);
  EXPECT_EQ(info->type, kFakeSubsystemUfs);
  EXPECT_EQ(info->size, kFakeSize);
  EXPECT_EQ(info->read_time_seconds_since_last_boot, 198);
  EXPECT_EQ(info->write_time_seconds_since_last_boot, 89345);
  EXPECT_EQ(info->bytes_read_since_last_boot,
            (uint64_t)14995718 * kFakeBlockSize);
  EXPECT_EQ(info->bytes_written_since_last_boot,
            (uint64_t)325649111 * kFakeBlockSize);
  EXPECT_EQ(info->io_time_seconds_since_last_boot, 7221);
  EXPECT_EQ(info->discard_time_seconds_since_last_boot->value, 194);
  EXPECT_EQ(info->vendor_id->get_jedec_manfid(), 0x1337);
  EXPECT_EQ(info->product_id->get_other(), 0);
  EXPECT_EQ(info->revision->get_other(), 0);
  EXPECT_EQ(info->name, "MYUFS");
  EXPECT_EQ(info->firmware_string, "2022");
  EXPECT_EQ(info->firmware_version->get_ufs_fwrev(), 0x32323032);
  EXPECT_TRUE(info->device_info->is_ufs_device_info());
  EXPECT_EQ(info->device_info->get_ufs_device_info()->jedec_manfid, 0x1337);
  EXPECT_EQ(info->device_info->get_ufs_device_info()->fwrev, 0x32323032);
  EXPECT_EQ(info->purpose, kFakePurpose);
  EXPECT_EQ(info->manufacturer_id, 0);
  EXPECT_EQ(info->serial, 0);
}

TEST_F(StorageDeviceInfoTest, FetchUFSTestWithNoData) {
  constexpr char kPath[] =
      "cros_healthd/fetchers/storage/testdata/sys/block/sdb";
  auto mock_platform = std::make_unique<StrictMock<MockPlatform>>();
  auto dev_info = StorageDeviceInfo::Create(
      base::FilePath(kPath), base::FilePath(kFakeDevnode), kFakeSubsystemUfs,
      kFakePurpose, mock_platform.get());
  EXPECT_EQ(dev_info, nullptr);
}

TEST_F(StorageDeviceInfoTest, FetchSataTest) {
  constexpr char kPath[] =
      "cros_healthd/fetchers/storage/testdata/sys/block/sdc";
  auto mock_platform = CreateMockPlatform();
  auto dev_info = StorageDeviceInfo::Create(
      base::FilePath(kPath), base::FilePath(kFakeDevnode), kFakeSubsystemSata,
      kFakePurpose, mock_platform.get());
  EXPECT_NE(dev_info, nullptr);

  auto info_result = dev_info->FetchDeviceInfo();
  EXPECT_TRUE(info_result.has_value());

  auto info = std::move(info_result.value());
  EXPECT_EQ(info->path, kFakeDevnode);
  EXPECT_EQ(info->type, kFakeSubsystemSata);
  EXPECT_EQ(info->size, kFakeSize);
  EXPECT_EQ(info->read_time_seconds_since_last_boot, 4);
  EXPECT_EQ(info->write_time_seconds_since_last_boot, 162);
  EXPECT_EQ(info->bytes_read_since_last_boot,
            (uint64_t)1011383 * kFakeBlockSize);
  EXPECT_EQ(info->bytes_written_since_last_boot,
            (uint64_t)1242744 * kFakeBlockSize);
  EXPECT_EQ(info->io_time_seconds_since_last_boot, 38);
  EXPECT_EQ(info->discard_time_seconds_since_last_boot->value, 0);
  EXPECT_EQ(info->vendor_id->get_other(), 0);
  EXPECT_EQ(info->product_id->get_other(), 0);
  EXPECT_EQ(info->revision->get_other(), 0);
  EXPECT_EQ(info->name, "BAR SATA");
  EXPECT_EQ(info->firmware_string, "");
  EXPECT_EQ(info->firmware_version->get_other(), 0);
  EXPECT_TRUE(info->device_info.is_null());
  EXPECT_EQ(kFakePurpose, info->purpose);
  EXPECT_EQ(info->manufacturer_id, 0);
  EXPECT_EQ(info->serial, 0);
}

}  // namespace
}  // namespace diagnostics
