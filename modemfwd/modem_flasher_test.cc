// Copyright 2017 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "modemfwd/modem_flasher.h"

#include <memory>
#include <utility>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/scoped_temp_dir.h>
#include <chromeos/switches/modemfwd_switches.h>
#include <dbus/modemfwd/dbus-constants.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "modemfwd/firmware_directory_stub.h"
#include "modemfwd/mock_modem.h"

using ::testing::_;
using ::testing::AtLeast;
using ::testing::Mock;
using ::testing::Return;

namespace modemfwd {

namespace {

constexpr char kDeviceId1[] = "device:id:1";
constexpr char kDeviceId2[] = "device:id:2";
constexpr char kEquipmentId1[] = "equipment_id_1";

constexpr char kMainFirmware1Path[] = "main_fw_1.fls";
constexpr char kMainFirmware1Version[] = "versionA";

constexpr char kMainFirmware2Path[] = "main_fw_2.fls";
constexpr char kMainFirmware2Version[] = "versionB";

constexpr char kOemFirmware1Path[] = "oem_cust_1.fls";
constexpr char kOemFirmware1Version[] = "6000.1";

constexpr char kOemFirmware2Path[] = "oem_cust_2.fls";
constexpr char kOemFirmware2Version[] = "6000.2";

constexpr char kCarrier1[] = "uuid_1";
constexpr char kCarrier1Mvno[] = "uuid_1_1";
constexpr char kCarrier1Firmware1Path[] = "carrier_1_fw_1.fls";
constexpr char kCarrier1Firmware1Version[] = "v1.00";
constexpr char kCarrier1Firmware2Path[] = "carrier_1_fw_2.fls";
constexpr char kCarrier1Firmware2Version[] = "v1.10";

constexpr char kCarrier2[] = "uuid_2";
constexpr char kCarrier2Firmware1Path[] = "carrier_2_fw_1.fls";
constexpr char kCarrier2Firmware1Version[] = "4500.15.65";

constexpr char kGenericCarrierFirmware1Path[] = "generic_fw_1.fls";
constexpr char kGenericCarrierFirmware1Version[] = "2017-10-13";
constexpr char kGenericCarrierFirmware2Path[] = "generic_fw_2.fls";
constexpr char kGenericCarrierFirmware2Version[] = "2017-10-14";

// Associated payloads
constexpr char kApFirmwareTag[] = "ap";
constexpr char kApFirmware1Path[] = "ap_firmware";
constexpr char kApFirmware1Version[] = "abc.a40";

constexpr char kApFirmware2Path[] = "ap_firmware_2";
constexpr char kApFirmware2Version[] = "def.g50";

constexpr char kDevFirmwareTag[] = "dev";
constexpr char kDevFirmwarePath[] = "dev_firmware";
constexpr char kDevFirmwareVersion[] = "000.012";

}  // namespace

class ModemFlasherTest : public ::testing::Test {
 public:
  ModemFlasherTest() {
    firmware_directory_ =
        std::make_unique<FirmwareDirectoryStub>(base::FilePath());

    CHECK(prefs_dir_.CreateUniqueTempDir());
    modems_seen_since_oobe_prefs_ = Prefs::CreatePrefs(prefs_dir_.GetPath());

    modem_flasher_ = CreateModemFlasher(firmware_directory_.get(),
                                        modems_seen_since_oobe_prefs_.get());
  }

 protected:
  void AddMainFirmwareFile(const std::string& device_id,
                           const base::FilePath& rel_firmware_path,
                           const std::string& version) {
    FirmwareFileInfo firmware_info(rel_firmware_path.value(), version);
    firmware_directory_->AddMainFirmware(kDeviceId1, firmware_info);
  }

  void AddAssocFirmwareFile(const std::string& main_fw_path,
                            const std::string& firmware_id,
                            const base::FilePath& rel_firmware_path,
                            const std::string& version) {
    FirmwareFileInfo firmware_info(rel_firmware_path.value(), version);
    firmware_directory_->AddAssocFirmware(main_fw_path, firmware_id,
                                          firmware_info);
  }

  void AddMainFirmwareFileForCarrier(const std::string& device_id,
                                     const std::string& carrier_name,
                                     const base::FilePath& rel_firmware_path,
                                     const std::string& version) {
    FirmwareFileInfo firmware_info(rel_firmware_path.value(), version);
    firmware_directory_->AddMainFirmwareForCarrier(kDeviceId1, carrier_name,
                                                   firmware_info);
  }

  void AddOemFirmwareFile(const std::string& device_id,
                          const base::FilePath& rel_firmware_path,
                          const std::string& version) {
    FirmwareFileInfo firmware_info(rel_firmware_path.value(), version);
    firmware_directory_->AddOemFirmware(kDeviceId1, firmware_info);
  }

  void AddOemFirmwareFileForCarrier(const std::string& device_id,
                                    const std::string& carrier_name,
                                    const base::FilePath& rel_firmware_path,
                                    const std::string& version) {
    FirmwareFileInfo firmware_info(rel_firmware_path.value(), version);
    firmware_directory_->AddOemFirmwareForCarrier(kDeviceId1, carrier_name,
                                                  firmware_info);
  }

  void AddCarrierFirmwareFile(const std::string& device_id,
                              const std::string& carrier_name,
                              const base::FilePath& rel_firmware_path,
                              const std::string& version) {
    FirmwareFileInfo firmware_info(rel_firmware_path.value(), version);
    firmware_directory_->AddCarrierFirmware(kDeviceId1, carrier_name,
                                            firmware_info);
  }

  std::unique_ptr<MockModem> GetDefaultModem() {
    auto modem = std::make_unique<MockModem>();
    ON_CALL(*modem, GetDeviceId()).WillByDefault(Return(kDeviceId1));
    ON_CALL(*modem, GetEquipmentId()).WillByDefault(Return(kEquipmentId1));
    ON_CALL(*modem, GetCarrierId()).WillByDefault(Return(kCarrier1));
    ON_CALL(*modem, GetMainFirmwareVersion())
        .WillByDefault(Return(kMainFirmware1Version));
    ON_CALL(*modem, GetOemFirmwareVersion())
        .WillByDefault(Return(kOemFirmware1Version));
    ON_CALL(*modem, GetCarrierFirmwareId()).WillByDefault(Return(""));
    ON_CALL(*modem, GetCarrierFirmwareVersion()).WillByDefault(Return(""));
    modems_seen_since_oobe_prefs_->Create(kDeviceId1);
    return modem;
  }

  void SetCarrierFirmwareInfo(MockModem* modem,
                              const std::string& carrier_id,
                              const std::string& version) {
    ON_CALL(*modem, GetCarrierFirmwareId()).WillByDefault(Return(carrier_id));
    ON_CALL(*modem, GetCarrierFirmwareVersion()).WillByDefault(Return(version));
  }

  brillo::ErrorPtr err;
  std::unique_ptr<ModemFlasher> modem_flasher_;

  base::ScopedTempDir prefs_dir_;
  std::unique_ptr<Prefs> modems_seen_since_oobe_prefs_;

 private:
  std::unique_ptr<FirmwareDirectoryStub> firmware_directory_;
};

TEST_F(ModemFlasherTest, NewModemIsFlashable) {
  auto modem = GetDefaultModem();
  EXPECT_TRUE(modem_flasher_->ShouldFlash(modem.get(), &err));
  ASSERT_EQ(err.get(), nullptr);
}

TEST_F(ModemFlasherTest, EmptyConfigFromEmptyFirmwareDirectory) {
  auto modem = GetDefaultModem();
  auto cfg = modem_flasher_->BuildFlashConfig(modem.get(), &err);

  ASSERT_TRUE(cfg->fw_configs.empty());
  ASSERT_TRUE(cfg->files.empty());
  ASSERT_EQ(err.get(), nullptr);
}

TEST_F(ModemFlasherTest, NewMainFirmwareAvailable) {
  const base::FilePath new_firmware(kMainFirmware2Path);
  AddMainFirmwareFile(kDeviceId1, new_firmware, kMainFirmware2Version);
  const std::vector<FirmwareConfig> main_cfg = {
      {kFwMain, new_firmware, kMainFirmware2Version}};

  auto modem = GetDefaultModem();
  auto cfg = modem_flasher_->BuildFlashConfig(modem.get(), &err);

  ASSERT_EQ(cfg->fw_configs, main_cfg);
  ASSERT_EQ(cfg->files[kFwMain]->path_on_filesystem(), new_firmware);
  ASSERT_EQ(err.get(), nullptr);
}

TEST_F(ModemFlasherTest, EmptyConfigFromSameMainFirmware) {
  const base::FilePath firmware(kMainFirmware1Path);
  AddMainFirmwareFile(kDeviceId1, firmware, kMainFirmware1Version);

  auto modem = GetDefaultModem();
  auto cfg = modem_flasher_->BuildFlashConfig(modem.get(), &err);
  ASSERT_TRUE(cfg->fw_configs.empty());
  ASSERT_TRUE(cfg->files.empty());
  ASSERT_EQ(err.get(), nullptr);
}

TEST_F(ModemFlasherTest, NewOemFirmwareAvailable) {
  const base::FilePath new_firmware(kOemFirmware2Path);
  AddOemFirmwareFile(kDeviceId1, new_firmware, kOemFirmware2Version);
  std::vector<FirmwareConfig> oem_cfg = {
      {kFwOem, new_firmware, kOemFirmware2Version}};

  auto modem = GetDefaultModem();
  auto cfg = modem_flasher_->BuildFlashConfig(modem.get(), &err);
  ASSERT_EQ(cfg->fw_configs, oem_cfg);
  ASSERT_EQ(cfg->files[kFwOem]->path_on_filesystem(), new_firmware);
  ASSERT_EQ(err.get(), nullptr);
}

TEST_F(ModemFlasherTest, EmptyConfigFromSameOemFirmware) {
  const base::FilePath firmware(kOemFirmware1Path);
  AddOemFirmwareFile(kDeviceId1, firmware, kOemFirmware1Version);

  auto modem = GetDefaultModem();
  auto cfg = modem_flasher_->BuildFlashConfig(modem.get(), &err);
  ASSERT_TRUE(cfg->fw_configs.empty());
  ASSERT_TRUE(cfg->files.empty());
  ASSERT_EQ(err.get(), nullptr);
}

TEST_F(ModemFlasherTest, NewCarrierFirmwareAvailable) {
  const base::FilePath new_firmware(kCarrier1Firmware2Path);
  AddCarrierFirmwareFile(kDeviceId1, kCarrier1, new_firmware,
                         kCarrier1Firmware2Version);
  std::vector<FirmwareConfig> carrier_cfg = {
      {kFwCarrier, new_firmware, kCarrier1Firmware2Version}};

  auto modem = GetDefaultModem();
  SetCarrierFirmwareInfo(modem.get(), kCarrier1, kCarrier1Firmware1Version);

  auto cfg = modem_flasher_->BuildFlashConfig(modem.get(), &err);
  ASSERT_EQ(cfg->fw_configs, carrier_cfg);
  ASSERT_EQ(cfg->files[kFwCarrier]->path_on_filesystem(), new_firmware);
  ASSERT_EQ(err.get(), nullptr);
}

TEST_F(ModemFlasherTest, EmptyConfigFromSameCarrierFirmware) {
  const base::FilePath original_firmware(kCarrier1Firmware1Path);
  AddCarrierFirmwareFile(kDeviceId1, kCarrier1, original_firmware,
                         kCarrier1Firmware1Version);

  auto modem = GetDefaultModem();
  SetCarrierFirmwareInfo(modem.get(), kCarrier1, kCarrier1Firmware1Version);

  auto cfg = modem_flasher_->BuildFlashConfig(modem.get(), &err);
  ASSERT_TRUE(cfg->fw_configs.empty());
  ASSERT_TRUE(cfg->files.empty());
  ASSERT_EQ(err.get(), nullptr);
}

TEST_F(ModemFlasherTest, SwitchCarrier) {
  const base::FilePath carrier1_firmware(kCarrier1Firmware1Path);
  AddCarrierFirmwareFile(kDeviceId1, kCarrier1, carrier1_firmware,
                         kCarrier1Firmware1Version);
  const base::FilePath carrier2_firmware(kCarrier2Firmware1Path);
  AddCarrierFirmwareFile(kDeviceId1, kCarrier2, carrier2_firmware,
                         kCarrier2Firmware1Version);

  auto modem = GetDefaultModem();

  EXPECT_CALL(*modem, GetCarrierId()).WillRepeatedly(Return(kCarrier2));
  SetCarrierFirmwareInfo(modem.get(), kCarrier1, kCarrier1Firmware1Version);
  std::vector<FirmwareConfig> carrier2_cfg = {
      {kFwCarrier, carrier2_firmware, kCarrier2Firmware1Version}};
  auto cfg = modem_flasher_->BuildFlashConfig(modem.get(), &err);
  ASSERT_EQ(cfg->fw_configs, carrier2_cfg);
  ASSERT_EQ(cfg->files[kFwCarrier]->path_on_filesystem(), carrier2_firmware);
  ASSERT_EQ(err.get(), nullptr);

  EXPECT_CALL(*modem, GetCarrierId()).WillRepeatedly(Return(kCarrier1));
  SetCarrierFirmwareInfo(modem.get(), kCarrier2, kCarrier2Firmware1Version);
  std::vector<FirmwareConfig> carrier1_cfg = {
      {kFwCarrier, carrier1_firmware, kCarrier1Firmware1Version}};
  cfg = modem_flasher_->BuildFlashConfig(modem.get(), &err);
  ASSERT_EQ(cfg->fw_configs, carrier1_cfg);
  ASSERT_EQ(cfg->files[kFwCarrier]->path_on_filesystem(), carrier1_firmware);
  ASSERT_EQ(err.get(), nullptr);
}

TEST_F(ModemFlasherTest, SwitchCarrierWithMainFirmware) {
  const base::FilePath main1_firmware(kMainFirmware1Path);
  AddMainFirmwareFile(kDeviceId1, main1_firmware, kMainFirmware1Version);
  const base::FilePath main2_firmware(kMainFirmware2Path);
  AddMainFirmwareFileForCarrier(kDeviceId1, kCarrier2, main2_firmware,
                                kMainFirmware2Version);
  const base::FilePath carrier1_firmware(kCarrier1Firmware1Path);
  AddCarrierFirmwareFile(kDeviceId1, kCarrier1, carrier1_firmware,
                         kCarrier1Firmware1Version);
  const base::FilePath carrier2_firmware(kCarrier2Firmware1Path);
  AddCarrierFirmwareFile(kDeviceId1, kCarrier2, carrier2_firmware,
                         kCarrier2Firmware1Version);

  auto modem = GetDefaultModem();

  EXPECT_CALL(*modem, GetCarrierId()).WillRepeatedly(Return(kCarrier2));
  SetCarrierFirmwareInfo(modem.get(), kCarrier1, kCarrier1Firmware1Version);
  auto cfg = modem_flasher_->BuildFlashConfig(modem.get(), &err);

  ASSERT_EQ(std::ranges::count(
                cfg->fw_configs,
                FirmwareConfig{kFwMain, main2_firmware, kMainFirmware2Version}),
            1);
  ASSERT_EQ(std::ranges::count(cfg->fw_configs,
                               FirmwareConfig{kFwCarrier, carrier2_firmware,
                                              kCarrier2Firmware1Version}),
            1);

  ASSERT_EQ(cfg->files[kFwMain]->path_on_filesystem(), main2_firmware);
  ASSERT_EQ(cfg->files[kFwCarrier]->path_on_filesystem(), carrier2_firmware);
  ASSERT_EQ(err.get(), nullptr);

  EXPECT_CALL(*modem, GetCarrierId()).WillRepeatedly(Return(kCarrier1));
  EXPECT_CALL(*modem, GetMainFirmwareVersion())
      .WillRepeatedly(Return(kMainFirmware2Version));
  SetCarrierFirmwareInfo(modem.get(), kCarrier2, kCarrier2Firmware1Version);
  cfg = modem_flasher_->BuildFlashConfig(modem.get(), &err);

  ASSERT_EQ(std::ranges::count(
                cfg->fw_configs,
                FirmwareConfig{kFwMain, main1_firmware, kMainFirmware1Version}),
            1);
  ASSERT_EQ(std::ranges::count(cfg->fw_configs,
                               FirmwareConfig{kFwCarrier, carrier1_firmware,
                                              kCarrier1Firmware1Version}),
            1);

  ASSERT_EQ(cfg->files[kFwMain]->path_on_filesystem(), main1_firmware);
  ASSERT_EQ(cfg->files[kFwCarrier]->path_on_filesystem(), carrier1_firmware);
  ASSERT_EQ(err.get(), nullptr);
}

TEST_F(ModemFlasherTest, ShouldNotFlashAfterMainFlashFailure) {
  const base::FilePath new_firmware(kMainFirmware2Path);
  AddMainFirmwareFile(kDeviceId1, new_firmware, kMainFirmware2Version);
  auto modem = GetDefaultModem();
  auto cfg = modem_flasher_->BuildFlashConfig(modem.get(), &err);
  ASSERT_NE(cfg, nullptr);

  EXPECT_CALL(*modem, FlashFirmwares(_)).WillRepeatedly(Return(false));
  // The first flash failure should not block the modem.
  ASSERT_FALSE(modem_flasher_->RunFlash(modem.get(), *cfg, nullptr, &err));
  ASSERT_TRUE(modem_flasher_->ShouldFlash(modem.get(), &err));
  // The second one will.
  ASSERT_FALSE(modem_flasher_->RunFlash(modem.get(), *cfg, nullptr, &err));
  ASSERT_FALSE(modem_flasher_->ShouldFlash(modem.get(), &err));
}

TEST_F(ModemFlasherTest, ShouldNotFlashAfterCarrierFlashFailure) {
  const base::FilePath new_firmware(kCarrier1Firmware2Path);
  AddCarrierFirmwareFile(kDeviceId1, kCarrier1, new_firmware,
                         kCarrier1Firmware2Version);
  auto modem = GetDefaultModem();
  auto cfg = modem_flasher_->BuildFlashConfig(modem.get(), &err);
  ASSERT_NE(cfg, nullptr);

  EXPECT_CALL(*modem, FlashFirmwares(_)).WillRepeatedly(Return(false));
  // The first flash failure should not block the modem.
  ASSERT_FALSE(modem_flasher_->RunFlash(modem.get(), *cfg, nullptr, &err));
  ASSERT_TRUE(modem_flasher_->ShouldFlash(modem.get(), &err));
  // The second one will.
  ASSERT_FALSE(modem_flasher_->RunFlash(modem.get(), *cfg, nullptr, &err));
  ASSERT_FALSE(modem_flasher_->ShouldFlash(modem.get(), &err));
}

TEST_F(ModemFlasherTest, CacheLastFlashedMainFirmware) {
  base::FilePath new_firmware(kMainFirmware2Path);
  AddMainFirmwareFile(kDeviceId1, new_firmware, kMainFirmware2Version);

  auto modem = GetDefaultModem();
  auto cfg = modem_flasher_->BuildFlashConfig(modem.get(), &err);

  EXPECT_CALL(*modem, FlashFirmwares(_)).WillOnce(Return(true));
  ASSERT_TRUE(modem_flasher_->RunFlash(modem.get(), *cfg, nullptr, &err));
  ASSERT_EQ(err.get(), nullptr);

  // We've had issues in the past where the firmware version is updated
  // but the modem still reports the old version string. Refuse to flash
  // the main firmware twice because that should never be correct behavior
  // in one session. Otherwise, we might try to flash the main firmware
  // over and over.
  cfg = modem_flasher_->BuildFlashConfig(modem.get(), &err);
  ASSERT_TRUE(cfg->fw_configs.empty());
  ASSERT_TRUE(cfg->files.empty());
  ASSERT_EQ(err.get(), nullptr);
}

TEST_F(ModemFlasherTest, CacheLastFlashedOemFirmware) {
  const base::FilePath new_firmware(kOemFirmware2Path);
  AddOemFirmwareFile(kDeviceId1, new_firmware, kOemFirmware2Version);

  auto modem = GetDefaultModem();
  auto cfg = modem_flasher_->BuildFlashConfig(modem.get(), &err);

  EXPECT_CALL(*modem, FlashFirmwares(_)).WillOnce(Return(true));
  ASSERT_TRUE(modem_flasher_->RunFlash(modem.get(), *cfg, nullptr, &err));
  ASSERT_EQ(err.get(), nullptr);

  cfg = modem_flasher_->BuildFlashConfig(modem.get(), &err);
  ASSERT_TRUE(cfg->fw_configs.empty());
  ASSERT_TRUE(cfg->files.empty());
  ASSERT_EQ(err.get(), nullptr);
}

TEST_F(ModemFlasherTest, CacheLastFlashedCarrierFirmware) {
  const base::FilePath new_firmware(kCarrier1Firmware2Path);
  AddCarrierFirmwareFile(kDeviceId1, kCarrier1, new_firmware,
                         kCarrier1Firmware2Version);

  auto modem = GetDefaultModem();
  SetCarrierFirmwareInfo(modem.get(), kCarrier1, kCarrier1Firmware1Version);
  auto cfg = modem_flasher_->BuildFlashConfig(modem.get(), &err);

  EXPECT_CALL(*modem, FlashFirmwares(_)).WillOnce(Return(true));
  ASSERT_TRUE(modem_flasher_->RunFlash(modem.get(), *cfg, nullptr, &err));
  ASSERT_EQ(err.get(), nullptr);

  cfg = modem_flasher_->BuildFlashConfig(modem.get(), &err);
  ASSERT_TRUE(cfg->fw_configs.empty());
  ASSERT_TRUE(cfg->files.empty());
  ASSERT_EQ(err.get(), nullptr);
}

TEST_F(ModemFlasherTest, UpgradeGenericCarrierFirmware) {
  const base::FilePath new_firmware(kGenericCarrierFirmware2Path);
  AddCarrierFirmwareFile(kDeviceId1, FirmwareDirectory::kGenericCarrierId,
                         new_firmware, kGenericCarrierFirmware2Version);
  std::vector<FirmwareConfig> carrier_cfg = {
      {kFwCarrier, new_firmware, kGenericCarrierFirmware2Version}};

  auto modem = GetDefaultModem();
  EXPECT_CALL(*modem, GetCarrierId()).WillRepeatedly(Return(kCarrier1));
  SetCarrierFirmwareInfo(modem.get(), FirmwareDirectory::kGenericCarrierId,
                         kGenericCarrierFirmware1Version);

  auto cfg = modem_flasher_->BuildFlashConfig(modem.get(), &err);
  ASSERT_EQ(cfg->carrier_id, FirmwareDirectory::kGenericCarrierId);
  ASSERT_EQ(cfg->fw_configs, carrier_cfg);
  ASSERT_EQ(cfg->files[kFwCarrier]->path_on_filesystem(), new_firmware);
  ASSERT_EQ(err.get(), nullptr);
}

TEST_F(ModemFlasherTest, EmptyConfigFromSameGenericCarrierFirmware) {
  const base::FilePath original_firmware(kGenericCarrierFirmware1Path);
  AddCarrierFirmwareFile(kDeviceId1, FirmwareDirectory::kGenericCarrierId,
                         original_firmware, kGenericCarrierFirmware1Version);

  auto modem = GetDefaultModem();
  EXPECT_CALL(*modem, GetCarrierId()).WillRepeatedly(Return(kCarrier1));
  SetCarrierFirmwareInfo(modem.get(), FirmwareDirectory::kGenericCarrierId,
                         kGenericCarrierFirmware1Version);

  // Even if the reported carrier is not strictly the same, we should still know
  // not to try and reflash the generic carrier firmware.
  auto cfg = modem_flasher_->BuildFlashConfig(modem.get(), &err);
  ASSERT_TRUE(cfg->fw_configs.empty());
  ASSERT_TRUE(cfg->files.empty());
  ASSERT_EQ(err.get(), nullptr);
}

TEST_F(ModemFlasherTest, SwitchCarrierWithGeneric) {
  const base::FilePath carrier1_firmware(kCarrier1Firmware1Path);
  AddCarrierFirmwareFile(kDeviceId1, kCarrier1, carrier1_firmware,
                         kCarrier1Firmware1Version);
  const base::FilePath generic_firmware(kGenericCarrierFirmware1Path);
  AddCarrierFirmwareFile(kDeviceId1, FirmwareDirectory::kGenericCarrierId,
                         generic_firmware, kGenericCarrierFirmware1Version);

  auto modem = GetDefaultModem();

  EXPECT_CALL(*modem, GetCarrierId()).WillRepeatedly(Return(kCarrier2));
  SetCarrierFirmwareInfo(modem.get(), kCarrier1, kCarrier1Firmware1Version);
  std::vector<FirmwareConfig> generic_cfg = {
      {kFwCarrier, generic_firmware, kGenericCarrierFirmware1Version}};
  auto cfg = modem_flasher_->BuildFlashConfig(modem.get(), &err);
  ASSERT_EQ(cfg->fw_configs, generic_cfg);
  ASSERT_EQ(cfg->files[kFwCarrier]->path_on_filesystem(), generic_firmware);
  ASSERT_EQ(err.get(), nullptr);

  EXPECT_CALL(*modem, GetCarrierId()).WillRepeatedly(Return(kCarrier1));
  SetCarrierFirmwareInfo(modem.get(), FirmwareDirectory::kGenericCarrierId,
                         kGenericCarrierFirmware1Version);
  std::vector<FirmwareConfig> carrier1_cfg = {
      {kFwCarrier, carrier1_firmware, kCarrier1Firmware1Version}};
  cfg = modem_flasher_->BuildFlashConfig(modem.get(), &err);
  ASSERT_EQ(cfg->fw_configs, carrier1_cfg);
  ASSERT_EQ(cfg->files[kFwCarrier]->path_on_filesystem(), carrier1_firmware);
  ASSERT_EQ(err.get(), nullptr);
}

TEST_F(ModemFlasherTest, SkipCarrierWithTwoUuidSameFirmware) {
  base::FilePath current_firmware(kCarrier1Firmware1Path);
  AddCarrierFirmwareFile(kDeviceId1, kCarrier1, current_firmware,
                         kCarrier1Firmware2Version);
  AddCarrierFirmwareFile(kDeviceId1, kCarrier1Mvno, current_firmware,
                         kCarrier1Firmware2Version);

  auto modem = GetDefaultModem();
  // The modem will say that the currently flashed firmware has the carrier UUID
  // KCarrier1Mvno while the current carrier UUID is always returned as
  // kCarrier1.
  SetCarrierFirmwareInfo(modem.get(), kCarrier1Mvno, kCarrier1Firmware2Version);

  auto cfg = modem_flasher_->BuildFlashConfig(modem.get(), &err);
  ASSERT_TRUE(cfg->fw_configs.empty());
  ASSERT_TRUE(cfg->files.empty());
  ASSERT_EQ(err.get(), nullptr);
}

TEST_F(ModemFlasherTest, NoCarrier) {
  const base::FilePath carrier1_firmware(kCarrier1Firmware1Path);
  AddCarrierFirmwareFile(kDeviceId1, kCarrier1, carrier1_firmware,
                         kCarrier1Firmware1Version);
  const base::FilePath generic_firmware(kGenericCarrierFirmware1Path);
  AddCarrierFirmwareFile(kDeviceId1, FirmwareDirectory::kGenericCarrierId,
                         generic_firmware, kGenericCarrierFirmware1Version);

  auto modem = GetDefaultModem();
  EXPECT_CALL(*modem, GetCarrierId()).WillRepeatedly(Return(""));
  SetCarrierFirmwareInfo(modem.get(), kCarrier2, kCarrier2Firmware1Version);

  auto cfg = modem_flasher_->BuildFlashConfig(modem.get(), &err);
  // We shouldn't try to pick up any carrier firmware at all, even the generic
  // one, when we have no carrier.
  ASSERT_EQ(cfg->carrier_id, "");
  ASSERT_TRUE(cfg->fw_configs.empty());
  ASSERT_TRUE(cfg->files.empty());
  ASSERT_EQ(err.get(), nullptr);
}

TEST_F(ModemFlasherTest, ConfigHasAssocFirmware) {
  const base::FilePath main_fw_path(kMainFirmware2Path);
  AddMainFirmwareFile(kDeviceId1, main_fw_path, kMainFirmware2Version);
  const base::FilePath ap_fw_path(kApFirmware1Path);
  AddAssocFirmwareFile(kMainFirmware2Path, kApFirmwareTag, ap_fw_path,
                       kApFirmware1Version);
  const base::FilePath dev_fw_path(kDevFirmwarePath);
  AddAssocFirmwareFile(kMainFirmware2Path, kDevFirmwareTag, dev_fw_path,
                       kDevFirmwareVersion);

  auto modem = GetDefaultModem();
  auto cfg = modem_flasher_->BuildFlashConfig(modem.get(), &err);

  ASSERT_EQ(std::ranges::count(
                cfg->fw_configs,
                FirmwareConfig{kFwMain, main_fw_path, kMainFirmware2Version}),
            1);
  ASSERT_EQ(std::ranges::count(cfg->fw_configs,
                               FirmwareConfig{kApFirmwareTag, ap_fw_path,
                                              kApFirmware1Version}),
            1);
  ASSERT_EQ(std::ranges::count(cfg->fw_configs,
                               FirmwareConfig{kDevFirmwareTag, dev_fw_path,
                                              kDevFirmwareVersion}),
            1);

  ASSERT_EQ(cfg->files[kFwMain]->path_on_filesystem(), main_fw_path);
  ASSERT_EQ(cfg->files[kApFirmwareTag]->path_on_filesystem(), ap_fw_path);
  ASSERT_EQ(cfg->files[kDevFirmwareTag]->path_on_filesystem(), dev_fw_path);

  ASSERT_EQ(err.get(), nullptr);
}

TEST_F(ModemFlasherTest, UpgradeAssocFirmwareOnly) {
  const base::FilePath main_fw_path(kMainFirmware1Path);
  AddMainFirmwareFile(kDeviceId1, main_fw_path, kMainFirmware1Version);
  const base::FilePath ap_fw_path(kApFirmware2Path);
  AddAssocFirmwareFile(kMainFirmware1Path, kApFirmwareTag, ap_fw_path,
                       kApFirmware2Version);

  auto modem = GetDefaultModem();
  ON_CALL(*modem, GetAssocFirmwareVersion(kApFirmwareTag))
      .WillByDefault(Return(kApFirmware1Version));
  auto cfg = modem_flasher_->BuildFlashConfig(modem.get(), &err);

  std::vector<FirmwareConfig> ap_cfg = {
      {kApFirmwareTag, ap_fw_path, kApFirmware2Version}};
  cfg = modem_flasher_->BuildFlashConfig(modem.get(), &err);
  ASSERT_EQ(cfg->fw_configs, ap_cfg);
  ASSERT_EQ(cfg->files[kApFirmwareTag]->path_on_filesystem(), ap_fw_path);
  ASSERT_EQ(err.get(), nullptr);
}

TEST_F(ModemFlasherTest, ModemNeverSeenError) {
  base::FilePath new_firmware(kMainFirmware2Path);
  AddMainFirmwareFile(kDeviceId1, new_firmware, kMainFirmware2Version);

  auto modem = GetDefaultModem();
  auto cfg = modem_flasher_->BuildFlashConfig(modem.get(), &err);
  ASSERT_NE(cfg, nullptr);

  EXPECT_CALL(*modem, FlashFirmwares(_)).WillRepeatedly(Return(false));

  ASSERT_FALSE(modem_flasher_->RunFlash(modem.get(), *cfg, nullptr, &err));
  ASSERT_NE(err.get(), nullptr);
  ASSERT_EQ(err.get()->GetCode(), kErrorResultFailureReturnedByHelper);

  EXPECT_CALL(*modem, GetDeviceId()).WillRepeatedly(Return(kDeviceId2));
  ASSERT_FALSE(modem_flasher_->RunFlash(modem.get(), *cfg, nullptr, &err));
  ASSERT_NE(err.get(), nullptr);
  ASSERT_EQ(err.get()->GetCode(),
            kErrorResultFailureReturnedByHelperModemNeverSeen);
}

}  // namespace modemfwd
