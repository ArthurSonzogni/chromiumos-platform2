// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "modemfwd/flash_task.h"

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/run_loop.h>
#include <base/test/task_environment.h>
#include <base/time/time.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "modemfwd/mock_daemon_delegate.h"
#include "modemfwd/mock_journal.h"
#include "modemfwd/mock_metrics.h"
#include "modemfwd/mock_modem.h"
#include "modemfwd/mock_modem_flasher.h"
#include "modemfwd/mock_notification_manager.h"

using testing::_;
using testing::Return;

namespace modemfwd {

namespace {
constexpr char kDeviceId1[] = "device:id:1";
constexpr char kEquipmentId1[] = "equipment_id_1";

constexpr char kMainFirmware1Version[] = "versionA";

constexpr char kMainFirmware2Path[] = "main_fw_2.fls";
constexpr char kMainFirmware2Version[] = "versionB";

constexpr char kOemFirmware1Version[] = "6000.1";

constexpr char kCarrier1[] = "uuid_1";
constexpr char kCarrier2[] = "uuid_2";
constexpr char kCarrier2Firmware1Path[] = "carrier_2_fw_1.fls";
constexpr char kCarrier2Firmware1Version[] = "4500.15.65";

// Journal entry ID
constexpr char kJournalEntryId[] = "journal-entry";

constexpr char kFirmwareDir[] = "/fake/firmware/dir";
}  // namespace

class FlashTaskTest : public ::testing::Test {
 public:
  FlashTaskTest() {
    delegate_ = std::make_unique<MockDelegate>();
    journal_ = std::make_unique<MockJournal>();
    notification_mgr_ = std::make_unique<MockNotificationManager>();
    metrics_ = std::make_unique<MockMetrics>();
    modem_flasher_ = std::make_unique<MockModemFlasher>();
  }

  std::unique_ptr<FlashTask> CreateFlashTask() {
    return std::make_unique<FlashTask>(delegate_.get(), journal_.get(),
                                       notification_mgr_.get(), metrics_.get(),
                                       modem_flasher_.get());
  }

 protected:
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
    return modem;
  }

  void SetCarrierFirmwareInfo(MockModem* modem,
                              const std::string& carrier_id,
                              const std::string& version) {
    ON_CALL(*modem, GetCarrierFirmwareId()).WillByDefault(Return(carrier_id));
    ON_CALL(*modem, GetCarrierFirmwareVersion()).WillByDefault(Return(version));
  }

  std::unique_ptr<FlashConfig> GetConfig(const std::string& carrier_id,
                                         std::vector<FirmwareConfig> fw_cfgs) {
    std::map<std::string, std::unique_ptr<FirmwareFile>> files;
    for (const auto& fw_cfg : fw_cfgs) {
      auto file = std::make_unique<FirmwareFile>();
      file->PrepareFrom(base::FilePath(kFirmwareDir),
                        FirmwareFileInfo(fw_cfg.path.value(), fw_cfg.version));
      files[fw_cfg.fw_type] = std::move(file);
    }
    return std::make_unique<FlashConfig>(carrier_id, std::move(fw_cfgs),
                                         std::move(files));
  }

  std::unique_ptr<MockDelegate> delegate_;
  std::unique_ptr<MockJournal> journal_;
  std::unique_ptr<MockNotificationManager> notification_mgr_;
  std::unique_ptr<MockMetrics> metrics_;
  std::unique_ptr<MockModemFlasher> modem_flasher_;
};

TEST_F(FlashTaskTest, ModemIsBlocked) {
  auto task = CreateFlashTask();
  auto modem = GetDefaultModem();

  EXPECT_CALL(*modem_flasher_, ShouldFlash(modem.get(), _))
      .WillOnce(Return(false));

  brillo::ErrorPtr err;
  EXPECT_FALSE(task->Start(modem.get(), true, &err));
}

TEST_F(FlashTaskTest, NothingToFlash) {
  auto task = CreateFlashTask();
  auto modem = GetDefaultModem();

  EXPECT_CALL(*modem_flasher_, ShouldFlash(modem.get(), _))
      .WillOnce(Return(true));
  EXPECT_CALL(*modem_flasher_, BuildFlashConfig(modem.get(), _))
      .WillOnce(Return(GetConfig(kCarrier1, {})));
  EXPECT_CALL(*modem_flasher_, RunFlash(modem.get(), _, _, _, _)).Times(0);

  brillo::ErrorPtr err;
  EXPECT_TRUE(task->Start(modem.get(), true, &err));
}

TEST_F(FlashTaskTest, BuildConfigReturnedError) {
  auto task = CreateFlashTask();
  auto modem = GetDefaultModem();

  EXPECT_CALL(*modem_flasher_, ShouldFlash(modem.get(), _))
      .WillOnce(Return(true));
  EXPECT_CALL(*modem_flasher_, BuildFlashConfig(modem.get(), _))
      .WillOnce(Return(std::unique_ptr<FlashConfig>()));
  EXPECT_CALL(*modem_flasher_, RunFlash(modem.get(), _, _, _, _)).Times(0);

  brillo::ErrorPtr err;
  EXPECT_FALSE(task->Start(modem.get(), true, &err));
}

TEST_F(FlashTaskTest, FlashFailure) {
  auto task = CreateFlashTask();
  auto modem = GetDefaultModem();
  base::FilePath new_firmware(kMainFirmware2Path);

  EXPECT_CALL(*modem_flasher_, ShouldFlash(modem.get(), _))
      .WillOnce(Return(true));
  EXPECT_CALL(*modem_flasher_, BuildFlashConfig(modem.get(), _))
      .WillOnce(Return(GetConfig(
          kCarrier1, {{kFwMain, new_firmware, kMainFirmware2Version}})));
  EXPECT_CALL(*modem_flasher_, RunFlash(modem.get(), _, _, _, _))
      .WillOnce(Return(false));

  brillo::ErrorPtr err;
  EXPECT_FALSE(task->Start(modem.get(), true, &err));
}

TEST_F(FlashTaskTest, FlashSuccess) {
  auto task = CreateFlashTask();
  auto modem = GetDefaultModem();
  base::FilePath new_firmware(kMainFirmware2Path);

  EXPECT_CALL(*modem_flasher_, ShouldFlash(modem.get(), _))
      .WillOnce(Return(true));
  EXPECT_CALL(*modem_flasher_, BuildFlashConfig(modem.get(), _))
      .WillOnce(Return(GetConfig(
          kCarrier1, {{kFwMain, new_firmware, kMainFirmware2Version}})));
  EXPECT_CALL(*modem_flasher_, RunFlash(modem.get(), _, _, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*metrics_, SendFwFlashTime(_)).Times(1);

  brillo::ErrorPtr err;
  EXPECT_TRUE(task->Start(modem.get(), true, &err));
  EXPECT_EQ(err.get(), nullptr);
}

TEST_F(FlashTaskTest, WritesToJournal) {
  auto task = CreateFlashTask();
  auto modem = GetDefaultModem();
  base::FilePath new_firmware(kMainFirmware2Path);

  EXPECT_CALL(*modem_flasher_, ShouldFlash(modem.get(), _))
      .WillOnce(Return(true));
  EXPECT_CALL(*modem_flasher_, BuildFlashConfig(modem.get(), _))
      .WillOnce(Return(GetConfig(
          kCarrier1, {{kFwMain, new_firmware, kMainFirmware2Version}})));
  EXPECT_CALL(*modem_flasher_, RunFlash(modem.get(), _, _, _, _))
      .WillOnce(Return(true));

  EXPECT_CALL(*journal_, MarkStartOfFlashingFirmware(_, kDeviceId1, _))
      .WillOnce(Return(kJournalEntryId));
  // The cleanup callback marks the end of flashing the firmware. The delegate
  // will typically run it once the modem comes back up.
  base::OnceClosure cb;
  EXPECT_CALL(*delegate_, RegisterOnModemReappearanceCallback(_, _))
      .WillOnce([&cb](const std::string& /*equipment_id*/,
                      base::OnceClosure reg_cb) { cb = std::move(reg_cb); });

  brillo::ErrorPtr err;
  EXPECT_TRUE(task->Start(modem.get(), true, &err));
  EXPECT_EQ(err.get(), nullptr);

  // Ensure the journal entry is closed by the registered callback.
  EXPECT_CALL(*journal_, MarkEndOfFlashingFirmware(kJournalEntryId)).Times(1);
  EXPECT_FALSE(cb.is_null());
  std::move(cb).Run();
}

TEST_F(FlashTaskTest, WritesCarrierToJournal) {
  auto task = CreateFlashTask();
  auto modem = GetDefaultModem();
  base::FilePath new_firmware(kCarrier2Firmware1Path);

  EXPECT_CALL(*modem_flasher_, ShouldFlash(modem.get(), _))
      .WillOnce(Return(true));
  EXPECT_CALL(*modem_flasher_, BuildFlashConfig(modem.get(), _))
      .WillOnce(Return(GetConfig(
          kCarrier2, {{kFwCarrier, new_firmware, kCarrier2Firmware1Version}})));
  EXPECT_CALL(*modem_flasher_, RunFlash(modem.get(), _, _, _, _))
      .WillOnce(Return(true));

  EXPECT_CALL(*journal_, MarkStartOfFlashingFirmware(_, kDeviceId1, kCarrier2))
      .WillOnce(Return(kJournalEntryId));
  brillo::ErrorPtr err;
  EXPECT_TRUE(task->Start(modem.get(), true, &err));
  EXPECT_EQ(err.get(), nullptr);
}

TEST_F(FlashTaskTest, WritesToJournalOnFailure) {
  auto task = CreateFlashTask();
  auto modem = GetDefaultModem();
  base::FilePath new_firmware(kMainFirmware2Path);

  EXPECT_CALL(*modem_flasher_, ShouldFlash(modem.get(), _))
      .WillOnce(Return(true));
  EXPECT_CALL(*modem_flasher_, BuildFlashConfig(modem.get(), _))
      .WillOnce(Return(GetConfig(
          kCarrier1, {{kFwMain, new_firmware, kMainFirmware2Version}})));
  EXPECT_CALL(*modem_flasher_, RunFlash(modem.get(), _, _, _, _))
      .WillOnce(Return(false));

  EXPECT_CALL(*journal_, MarkStartOfFlashingFirmware(_, kDeviceId1, _))
      .WillOnce(Return(kJournalEntryId));
  EXPECT_CALL(*journal_, MarkEndOfFlashingFirmware(kJournalEntryId)).Times(1);
  // We should complete inline on failure. No callback should be registered.
  EXPECT_CALL(*delegate_, RegisterOnModemReappearanceCallback(_, _)).Times(0);

  brillo::ErrorPtr err;
  EXPECT_FALSE(task->Start(modem.get(), true, &err));
}

TEST_F(FlashTaskTest, InhibitDuringFlash) {
  auto task = CreateFlashTask();
  auto modem = GetDefaultModem();
  base::FilePath new_firmware(kMainFirmware2Path);

  EXPECT_CALL(*modem_flasher_, ShouldFlash(modem.get(), _))
      .WillOnce(Return(true));
  EXPECT_CALL(*modem_flasher_, BuildFlashConfig(modem.get(), _))
      .WillOnce(Return(GetConfig(
          kCarrier1, {{kFwMain, new_firmware, kMainFirmware2Version}})));
  EXPECT_CALL(*modem_flasher_, RunFlash(modem.get(), _, _, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*modem, SetInhibited(true)).Times(1);
  EXPECT_CALL(*modem, SetInhibited(false)).Times(1);

  brillo::ErrorPtr err;
  EXPECT_TRUE(task->Start(modem.get(), true, &err));
  EXPECT_EQ(err.get(), nullptr);
}

}  // namespace modemfwd
