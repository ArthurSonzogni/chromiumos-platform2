// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "modemfwd/flash_task.h"

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/files/scoped_temp_dir.h>
#include <base/run_loop.h>
#include <base/test/task_environment.h>
#include <base/time/time.h>
#include <dbus/message.h>
#include <dbus/mock_bus.h>
#include <dbus/mock_object_proxy.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "modemfwd/error.h"
#include "modemfwd/mock_daemon_delegate.h"
#include "modemfwd/mock_journal.h"
#include "modemfwd/mock_metrics.h"
#include "modemfwd/mock_modem.h"
#include "modemfwd/mock_modem_flasher.h"
#include "modemfwd/mock_notification_manager.h"

using testing::_;
using testing::IsNull;
using testing::NotNull;
using testing::Return;
using testing::SetArgPointee;
using testing::WithArg;

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

    bus_ = base::MakeRefCounted<dbus::MockBus>(dbus::Bus::Options());
    upstart_object_proxy_ = base::MakeRefCounted<dbus::MockObjectProxy>(
        bus_.get(), UpstartJobController::kUpstartServiceName,
        dbus::ObjectPath(UpstartJobController::kUpstartPath));
    ON_CALL(*upstart_object_proxy_, CallMethodAndBlock(_, _))
        .WillByDefault([](dbus::MethodCall* method_call, int /*timeout_ms*/) {
          auto resp = dbus::Response::CreateEmpty();
          dbus::MessageWriter writer(resp.get());
          if (method_call->GetMember() == "GetAllJobs") {
            writer.AppendArrayOfObjectPaths(std::vector<dbus::ObjectPath>{
                dbus::ObjectPath(UpstartJobController::kHermesJobPath),
                dbus::ObjectPath(UpstartJobController::kModemHelperJobPath)});
          }
          return resp;
        });
    ON_CALL(*bus_, GetObjectProxy(
                       UpstartJobController::kUpstartServiceName,
                       dbus::ObjectPath(UpstartJobController::kUpstartPath)))
        .WillByDefault(Return(upstart_object_proxy_.get()));
    // We can use the same mock object for all jobs.
    job_object_proxy_ = base::MakeRefCounted<dbus::MockObjectProxy>(
        bus_.get(), UpstartJobController::kUpstartServiceName,
        dbus::ObjectPath("/job"));
    ON_CALL(*job_object_proxy_, CallMethodAndBlock(_, _))
        .WillByDefault([](dbus::MethodCall* method_call, int /*timeout_ms*/) {
          auto resp = dbus::Response::CreateEmpty();
          dbus::MessageWriter writer(resp.get());
          if (method_call->GetMember() == "Start" ||
              method_call->GetMember() == "GetInstance") {
            writer.AppendObjectPath(dbus::ObjectPath("/job"));
          }
          return resp;
        });
    ON_CALL(*bus_, GetObjectProxy(UpstartJobController::kUpstartServiceName, _))
        .WillByDefault(Return(job_object_proxy_.get()));

    auto modem_flasher = std::make_unique<MockModemFlasher>();
    modem_flasher_ = modem_flasher.get();

    async_modem_flasher_ =
        base::MakeRefCounted<AsyncModemFlasher>(std::move(modem_flasher));
  }

  std::unique_ptr<FlashTask> CreateFlashTask() {
    return std::make_unique<FlashTask>(delegate_.get(), journal_.get(),
                                       notification_mgr_.get(), metrics_.get(),
                                       bus_, async_modem_flasher_);
  }

 protected:
  std::unique_ptr<MockModem> GetDefaultModem() {
    auto modem = std::make_unique<MockModem>();
    ON_CALL(*modem, IsPresent()).WillByDefault(Return(true));
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

    base::ScopedTempDir temp_extraction_dir;
    EXPECT_TRUE(temp_extraction_dir.CreateUniqueTempDir());
    for (const auto& fw_cfg : fw_cfgs) {
      auto file = std::make_unique<FirmwareFile>();
      file->PrepareFrom(base::FilePath(kFirmwareDir),
                        temp_extraction_dir.GetPath(),
                        FirmwareFileInfo(fw_cfg.path.value(), fw_cfg.version));
      files[fw_cfg.fw_type] = std::move(file);
    }
    return std::make_unique<FlashConfig>(carrier_id, std::move(fw_cfgs),
                                         std::move(files),
                                         std::move(temp_extraction_dir));
  }

  brillo::ErrorPtr RunTask(FlashTask* task,
                           Modem* modem,
                           const FlashTask::Options& options) {
    base::RunLoop run_loop;
    brillo::ErrorPtr error;
    EXPECT_CALL(*delegate_, FinishTask(task, _))
        .WillOnce(WithArg<1>(
            [&error, quit = run_loop.QuitClosure()](brillo::ErrorPtr e) {
              error = std::move(e);
              std::move(quit).Run();
            }));
    task->Start(modem, options);
    run_loop.Run();
    return error;
  }

  base::test::TaskEnvironment task_environment_;

  std::unique_ptr<MockDelegate> delegate_;
  std::unique_ptr<MockJournal> journal_;
  std::unique_ptr<MockNotificationManager> notification_mgr_;
  std::unique_ptr<MockMetrics> metrics_;

  scoped_refptr<dbus::MockBus> bus_;
  scoped_refptr<dbus::MockObjectProxy> upstart_object_proxy_;
  scoped_refptr<dbus::MockObjectProxy> job_object_proxy_;

  MockModemFlasher* modem_flasher_;
  scoped_refptr<AsyncModemFlasher> async_modem_flasher_;
};

TEST_F(FlashTaskTest, ModemIsBlocked) {
  auto task = CreateFlashTask();
  auto modem = GetDefaultModem();

  EXPECT_CALL(*modem_flasher_, ShouldFlash(modem.get(), _))
      .WillOnce(WithArg<1>([](brillo::ErrorPtr* error) {
        *error = Error::Create(FROM_HERE, "foo", "foo");
        return false;
      }));

  auto error = RunTask(task.get(), modem.get(), FlashTask::Options{});
  EXPECT_NE(error, nullptr);
}

TEST_F(FlashTaskTest, NothingToFlash) {
  auto task = CreateFlashTask();
  auto modem = GetDefaultModem();

  EXPECT_CALL(*modem_flasher_, ShouldFlash(modem.get(), _))
      .WillOnce(Return(true));
  EXPECT_CALL(*modem_flasher_, BuildFlashConfig(modem.get(), _, _))
      .WillOnce(Return(GetConfig(kCarrier1, {})));
  EXPECT_CALL(*modem_flasher_, RunFlash(modem.get(), _, _, _)).Times(0);

  auto error = RunTask(task.get(), modem.get(), FlashTask::Options{});
  EXPECT_EQ(error, nullptr);
}

TEST_F(FlashTaskTest, BuildConfigReturnedError) {
  auto task = CreateFlashTask();
  auto modem = GetDefaultModem();

  EXPECT_CALL(*modem_flasher_, ShouldFlash(modem.get(), _))
      .WillOnce(Return(true));
  EXPECT_CALL(*modem_flasher_, BuildFlashConfig(modem.get(), _, _))
      .WillOnce(WithArg<2>([](brillo::ErrorPtr* error) {
        *error = Error::Create(FROM_HERE, "foo", "foo");
        return nullptr;
      }));
  EXPECT_CALL(*modem_flasher_, RunFlash(modem.get(), _, _, _)).Times(0);

  auto error = RunTask(task.get(), modem.get(), FlashTask::Options{});
  EXPECT_NE(error, nullptr);
}

TEST_F(FlashTaskTest, FlashFailure) {
  auto task = CreateFlashTask();
  auto modem = GetDefaultModem();
  base::FilePath new_firmware(kMainFirmware2Path);

  EXPECT_CALL(*modem_flasher_, ShouldFlash(modem.get(), _))
      .WillOnce(Return(true));
  EXPECT_CALL(*modem_flasher_, BuildFlashConfig(modem.get(), _, _))
      .WillOnce(Return(GetConfig(
          kCarrier1, {{kFwMain, new_firmware, kMainFirmware2Version}})));
  EXPECT_CALL(*modem_flasher_, RunFlash(modem.get(), _, _, _))
      .WillOnce(WithArg<3>([](brillo::ErrorPtr* error) {
        *error = Error::Create(FROM_HERE, "foo", "foo");
        return false;
      }));

  auto error = RunTask(task.get(), modem.get(), FlashTask::Options{});
  EXPECT_NE(error, nullptr);
}

TEST_F(FlashTaskTest, FlashSuccess) {
  auto task = CreateFlashTask();
  auto modem = GetDefaultModem();
  base::FilePath new_firmware(kMainFirmware2Path);

  EXPECT_CALL(*modem_flasher_, ShouldFlash(modem.get(), _))
      .WillOnce(Return(true));
  EXPECT_CALL(*modem_flasher_, BuildFlashConfig(modem.get(), _, _))
      .WillOnce(Return(GetConfig(
          kCarrier1, {{kFwMain, new_firmware, kMainFirmware2Version}})));
  EXPECT_CALL(*modem_flasher_, RunFlash(modem.get(), _, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*metrics_, SendFwFlashTime(_)).Times(1);

  // The cleanup callback marks the end of flashing the firmware. The delegate
  // will typically run it once the modem comes back up.
  EXPECT_CALL(*delegate_, RegisterOnModemReappearanceCallback(_, _))
      .WillOnce(WithArg<1>([](base::OnceClosure reg_cb) {
        base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
            FROM_HERE, std::move(reg_cb));
      }));

  auto error = RunTask(task.get(), modem.get(), FlashTask::Options{});
  EXPECT_EQ(error, nullptr);
}

TEST_F(FlashTaskTest, WritesToJournal) {
  auto task = CreateFlashTask();
  auto modem = GetDefaultModem();
  base::FilePath new_firmware(kMainFirmware2Path);

  EXPECT_CALL(*modem_flasher_, ShouldFlash(modem.get(), _))
      .WillOnce(Return(true));
  EXPECT_CALL(*modem_flasher_, BuildFlashConfig(modem.get(), _, _))
      .WillOnce(Return(GetConfig(
          kCarrier1, {{kFwMain, new_firmware, kMainFirmware2Version}})));
  EXPECT_CALL(*modem_flasher_, RunFlash(modem.get(), _, _, _))
      .WillOnce(Return(true));

  EXPECT_CALL(*journal_, MarkStartOfFlashingFirmware(_, kDeviceId1, _))
      .WillOnce(Return(kJournalEntryId));
  EXPECT_CALL(*journal_, MarkEndOfFlashingFirmware(kJournalEntryId)).Times(1);

  // The cleanup callback marks the end of flashing the firmware. The delegate
  // will typically run it once the modem comes back up.
  EXPECT_CALL(*delegate_, RegisterOnModemReappearanceCallback(_, _))
      .WillOnce(WithArg<1>([](base::OnceClosure reg_cb) {
        base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
            FROM_HERE, std::move(reg_cb));
      }));

  auto error = RunTask(task.get(), modem.get(), FlashTask::Options{});
  EXPECT_EQ(error, nullptr);
}

TEST_F(FlashTaskTest, WritesCarrierToJournal) {
  auto task = CreateFlashTask();
  auto modem = GetDefaultModem();
  base::FilePath new_firmware(kCarrier2Firmware1Path);

  EXPECT_CALL(*modem_flasher_, ShouldFlash(modem.get(), _))
      .WillOnce(Return(true));
  EXPECT_CALL(*modem_flasher_, BuildFlashConfig(modem.get(), _, _))
      .WillOnce(Return(GetConfig(
          kCarrier2, {{kFwCarrier, new_firmware, kCarrier2Firmware1Version}})));
  EXPECT_CALL(*modem_flasher_, RunFlash(modem.get(), _, _, _))
      .WillOnce(Return(true));

  EXPECT_CALL(*journal_, MarkStartOfFlashingFirmware(_, kDeviceId1, kCarrier2))
      .WillOnce(Return(kJournalEntryId));
  EXPECT_CALL(*delegate_, RegisterOnModemReappearanceCallback(_, _))
      .WillOnce(WithArg<1>([](base::OnceClosure reg_cb) {
        base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
            FROM_HERE, std::move(reg_cb));
      }));

  RunTask(task.get(), modem.get(), FlashTask::Options{});
}

TEST_F(FlashTaskTest, WritesToJournalOnFailure) {
  auto task = CreateFlashTask();
  auto modem = GetDefaultModem();
  base::FilePath new_firmware(kMainFirmware2Path);

  EXPECT_CALL(*modem_flasher_, ShouldFlash(modem.get(), _))
      .WillOnce(Return(true));
  EXPECT_CALL(*modem_flasher_, BuildFlashConfig(modem.get(), _, _))
      .WillOnce(Return(GetConfig(
          kCarrier1, {{kFwMain, new_firmware, kMainFirmware2Version}})));
  EXPECT_CALL(*modem_flasher_, RunFlash(modem.get(), _, _, _))
      .WillOnce(Return(false));

  EXPECT_CALL(*journal_, MarkStartOfFlashingFirmware(_, kDeviceId1, _))
      .WillOnce(Return(kJournalEntryId));
  EXPECT_CALL(*journal_, MarkEndOfFlashingFirmware(kJournalEntryId)).Times(1);
  // We should complete inline on failure. No callback should be registered.
  EXPECT_CALL(*delegate_, RegisterOnModemReappearanceCallback(_, _)).Times(0);

  RunTask(task.get(), modem.get(), FlashTask::Options{});
}

TEST_F(FlashTaskTest, InhibitDuringFlash) {
  auto task = CreateFlashTask();
  auto modem = GetDefaultModem();
  base::FilePath new_firmware(kMainFirmware2Path);

  EXPECT_CALL(*modem_flasher_, ShouldFlash(modem.get(), _))
      .WillOnce(Return(true));
  EXPECT_CALL(*modem_flasher_, BuildFlashConfig(modem.get(), _, _))
      .WillOnce(Return(GetConfig(
          kCarrier1, {{kFwMain, new_firmware, kMainFirmware2Version}})));
  EXPECT_CALL(*modem_flasher_, RunFlash(modem.get(), _, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*delegate_, RegisterOnModemReappearanceCallback(_, _))
      .WillOnce(WithArg<1>([](base::OnceClosure reg_cb) {
        base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
            FROM_HERE, std::move(reg_cb));
      }));

  EXPECT_CALL(*modem, SetInhibited(true)).Times(1);
  EXPECT_CALL(*modem, SetInhibited(false)).Times(1);

  RunTask(task.get(), modem.get(), FlashTask::Options{});
}

TEST_F(FlashTaskTest, IgnoreBlock) {
  auto task = CreateFlashTask();
  auto modem = GetDefaultModem();
  base::FilePath new_firmware(kMainFirmware2Path);

  EXPECT_CALL(*modem_flasher_, ShouldFlash(modem.get(), _))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(*modem_flasher_, BuildFlashConfig(modem.get(), _, _))
      .WillOnce(Return(GetConfig(
          kCarrier1, {{kFwMain, new_firmware, kMainFirmware2Version}})));
  EXPECT_CALL(*modem_flasher_, RunFlash(modem.get(), _, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*delegate_, RegisterOnModemReappearanceCallback(_, _))
      .WillOnce(WithArg<1>([](base::OnceClosure reg_cb) {
        base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
            FROM_HERE, std::move(reg_cb));
      }));

  auto error = RunTask(task.get(), modem.get(),
                       FlashTask::Options{.should_always_flash = true});
  EXPECT_EQ(error, nullptr);
}

TEST_F(FlashTaskTest, SyncCleanupForStubModem) {
  auto task = CreateFlashTask();
  auto modem = GetDefaultModem();
  base::FilePath new_firmware(kMainFirmware2Path);

  // Pretend this is a stub modem.
  EXPECT_CALL(*modem, IsPresent()).WillRepeatedly(Return(false));

  EXPECT_CALL(*modem_flasher_, ShouldFlash(modem.get(), _))
      .WillOnce(Return(true));
  EXPECT_CALL(*modem_flasher_, BuildFlashConfig(modem.get(), _, _))
      .WillOnce(Return(GetConfig(
          kCarrier1, {{kFwMain, new_firmware, kMainFirmware2Version}})));
  EXPECT_CALL(*modem_flasher_, RunFlash(modem.get(), _, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*metrics_, SendFwFlashTime(_)).Times(1);
  EXPECT_CALL(*journal_, MarkStartOfFlashingFirmware(_, kDeviceId1, _))
      .WillOnce(Return(kJournalEntryId));

  // We should expect this to run synchronously.
  EXPECT_CALL(*journal_, MarkEndOfFlashingFirmware(kJournalEntryId)).Times(1);

  RunTask(task.get(), modem.get(), FlashTask::Options{});
}

}  // namespace modemfwd
