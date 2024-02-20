// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "heartd/daemon/action_runner.h"

#include <fcntl.h>
#include <string>

#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/test/task_environment.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "heartd/daemon/test_utils/mock_dbus_connector.h"

namespace heartd {

namespace {

namespace mojom = ::ash::heartd::mojom;

using ::testing::_;
using ::testing::Exactly;

void InsertNormalRebootRecords(std::vector<BootRecord>& boot_records,
                               int count,
                               base::Time& time) {
  for (int i = 0; i < count; ++i) {
    boot_records.emplace_back("shutdown.123", time);
    boot_records.emplace_back("Boot ID", time);
  }
}

void InsertAbnormalRebootRecords(std::vector<BootRecord>& boot_records,
                                 int count,
                                 base::Time& time) {
  // Because two consecutive boot IDs indicate one abnormal reboot.
  for (int i = 0; i < count + 1; ++i) {
    boot_records.emplace_back("Boot ID", time);
  }
}

class ActionRunnerTest : public testing::Test {
 public:
  ActionRunnerTest() {}
  ~ActionRunnerTest() override = default;

  void VerifyForceReboot(bool is_reboot) {
    std::string content;
    EXPECT_TRUE(ReadFileToString(fake_sysrq_path_, &content));
    if (is_reboot) {
      EXPECT_EQ(content, "c");
    } else {
      EXPECT_EQ(content, "");
    }
  }

 protected:
  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    fake_sysrq_path_ = temp_dir_.GetPath().Append("sysrq-trigger");
    fake_sysrq_fd_ =
        open(fake_sysrq_path_.MaybeAsASCII().c_str(), O_CREAT | O_WRONLY, 0644);
    EXPECT_NE(fake_sysrq_fd_, -1);
    action_runner_.SetupSysrq(fake_sysrq_fd_);
  }
  void TearDown() override { ASSERT_TRUE(temp_dir_.Delete()); }

 protected:
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  base::ScopedTempDir temp_dir_;
  base::FilePath fake_sysrq_path_;
  int fake_sysrq_fd_;
  MockDbusConnector mock_dbus_connector_;
  ActionRunner action_runner_{&mock_dbus_connector_};
};

TEST_F(ActionRunnerTest, DefaultSetting) {
  EXPECT_CALL(*mock_dbus_connector_.power_manager_proxy(),
              RequestRestartAsync(_, _, _, _, _))
      .Times(Exactly(0));

  action_runner_.Run(mojom::ServiceName::kKiosk,
                     mojom::ActionType::kNormalReboot);
  action_runner_.Run(mojom::ServiceName::kKiosk,
                     mojom::ActionType::kForceReboot);
  VerifyForceReboot(/* is_reboot= */ false);
}

TEST_F(ActionRunnerTest, EnableNormalRebootActionForNormalReboot) {
  EXPECT_CALL(*mock_dbus_connector_.power_manager_proxy(),
              RequestRestartAsync(_, _, _, _, _))
      .Times(Exactly(1));

  action_runner_.EnableNormalRebootAction();
  action_runner_.Run(mojom::ServiceName::kKiosk,
                     mojom::ActionType::kNormalReboot);
}

TEST_F(ActionRunnerTest, EnableForceRebootActionForForceReboot) {
  action_runner_.SetSyncFlag(false);
  action_runner_.EnableForceRebootAction();
  action_runner_.Run(mojom::ServiceName::kKiosk,
                     mojom::ActionType::kForceReboot);
  VerifyForceReboot(/* is_reboot= */ true);
}

TEST_F(ActionRunnerTest, EnableNormalRebootActionForOtherAction) {
  EXPECT_CALL(*mock_dbus_connector_.power_manager_proxy(),
              RequestRestartAsync(_, _, _, _, _))
      .Times(Exactly(0));

  action_runner_.EnableNormalRebootAction();
  auto start_action = mojom::ActionType::kNoOperation;
  switch (start_action) {
    case mojom::ActionType::kNoOperation:
      action_runner_.Run(mojom::ServiceName::kKiosk,
                         mojom::ActionType::kNoOperation);
      [[fallthrough]];
    case mojom::ActionType::kNormalReboot:
      // It should reboot, it's tested in
      // EnableNormalRebootActionForNormalReboot.
      [[fallthrough]];
    case mojom::ActionType::kForceReboot:
      action_runner_.Run(mojom::ServiceName::kKiosk,
                         mojom::ActionType::kForceReboot);
      [[fallthrough]];
    case mojom::ActionType::kUnmappedEnumField:
      action_runner_.Run(mojom::ServiceName::kKiosk,
                         mojom::ActionType::kUnmappedEnumField);
      break;
  }
}

TEST_F(ActionRunnerTest, EnableForceRebootActionForOtherAction) {
  action_runner_.EnableForceRebootAction();
  auto start_action = mojom::ActionType::kNoOperation;
  switch (start_action) {
    case mojom::ActionType::kNoOperation:
      action_runner_.Run(mojom::ServiceName::kKiosk,
                         mojom::ActionType::kNoOperation);
      [[fallthrough]];
    case mojom::ActionType::kNormalReboot:
      action_runner_.Run(mojom::ServiceName::kKiosk,
                         mojom::ActionType::kNormalReboot);
      [[fallthrough]];
    case mojom::ActionType::kForceReboot:
      // It should reboot, it's tested in
      // EnableForceRebootActionForForceReboot.
      [[fallthrough]];
    case mojom::ActionType::kUnmappedEnumField:
      action_runner_.Run(mojom::ServiceName::kKiosk,
                         mojom::ActionType::kUnmappedEnumField);
      break;
  }

  VerifyForceReboot(/* is_reboot= */ false);
}

TEST_F(ActionRunnerTest, DisableNormalRebootAction) {
  EXPECT_CALL(*mock_dbus_connector_.power_manager_proxy(),
              RequestRestartAsync(_, _, _, _, _))
      .Times(Exactly(0));

  action_runner_.EnableNormalRebootAction();
  action_runner_.DisableNormalRebootAction();
  auto start_action = mojom::ActionType::kNoOperation;
  switch (start_action) {
    case mojom::ActionType::kNoOperation:
      action_runner_.Run(mojom::ServiceName::kKiosk,
                         mojom::ActionType::kNoOperation);
      [[fallthrough]];
    case mojom::ActionType::kNormalReboot:
      action_runner_.Run(mojom::ServiceName::kKiosk,
                         mojom::ActionType::kNormalReboot);
      [[fallthrough]];
    case mojom::ActionType::kForceReboot:
      action_runner_.Run(mojom::ServiceName::kKiosk,
                         mojom::ActionType::kForceReboot);
      [[fallthrough]];
    case mojom::ActionType::kUnmappedEnumField:
      action_runner_.Run(mojom::ServiceName::kKiosk,
                         mojom::ActionType::kUnmappedEnumField);
      break;
  }
}

TEST_F(ActionRunnerTest, DisableForceRebootAction) {
  action_runner_.EnableForceRebootAction();
  action_runner_.DisableForceRebootAction();
  auto start_action = mojom::ActionType::kNoOperation;
  switch (start_action) {
    case mojom::ActionType::kNoOperation:
      action_runner_.Run(mojom::ServiceName::kKiosk,
                         mojom::ActionType::kNoOperation);
      [[fallthrough]];
    case mojom::ActionType::kNormalReboot:
      action_runner_.Run(mojom::ServiceName::kKiosk,
                         mojom::ActionType::kNormalReboot);
      [[fallthrough]];
    case mojom::ActionType::kForceReboot:
      action_runner_.Run(mojom::ServiceName::kKiosk,
                         mojom::ActionType::kForceReboot);
      [[fallthrough]];
    case mojom::ActionType::kUnmappedEnumField:
      action_runner_.Run(mojom::ServiceName::kKiosk,
                         mojom::ActionType::kUnmappedEnumField);
      break;
  }

  VerifyForceReboot(/* is_reboot= */ false);
}

TEST_F(ActionRunnerTest, NormalReboot12HoursThreshold) {
  std::vector<BootRecord> boot_records;
  auto start_time = base::Time().Now();
  task_environment_.FastForwardBy(base::Hours(12));

  // 2 normal reboot records within 12 hours window.
  InsertNormalRebootRecords(boot_records, 2, start_time);
  action_runner_.CacheBootRecord(boot_records);
  EXPECT_FALSE(action_runner_.IsNormalRebootTooManyTimes());

  // 3 normal reboot records within 12 hours window.
  InsertNormalRebootRecords(boot_records, 1, start_time);
  action_runner_.CacheBootRecord(boot_records);
  EXPECT_TRUE(action_runner_.IsNormalRebootTooManyTimes());

  // If we move forward 1 second, we should be able to call the reboot action
  // again.
  task_environment_.FastForwardBy(base::Seconds(1));
  EXPECT_FALSE(action_runner_.IsNormalRebootTooManyTimes());
}

TEST_F(ActionRunnerTest, DryRunSuccess) {
  action_runner_.EnableNormalRebootAction();
  EXPECT_EQ(action_runner_.DryRun(mojom::ServiceName::kKiosk,
                                  mojom::ActionType::kNormalReboot),
            mojom::HeartbeatResponse::kSuccess);
}

TEST_F(ActionRunnerTest, DryRunFailure) {
  EXPECT_EQ(action_runner_.DryRun(mojom::ServiceName::kKiosk,
                                  mojom::ActionType::kNormalReboot),
            mojom::HeartbeatResponse::kNotAllowed);
}

TEST_F(ActionRunnerTest, DryRunRateLimit) {
  std::vector<BootRecord> boot_records;
  auto start_time = base::Time().Now();

  action_runner_.EnableNormalRebootAction();
  task_environment_.FastForwardBy(base::Days(7));
  InsertNormalRebootRecords(boot_records, 10, start_time);
  action_runner_.CacheBootRecord(boot_records);

  EXPECT_EQ(action_runner_.DryRun(mojom::ServiceName::kKiosk,
                                  mojom::ActionType::kNormalReboot),
            mojom::HeartbeatResponse::kRateLimit);
}

TEST_F(ActionRunnerTest, NormalReboot7DaysThreshold) {
  std::vector<BootRecord> boot_records;
  auto start_time = base::Time().Now();
  task_environment_.FastForwardBy(base::Days(7));

  // 9 normal reboot records within 7 days window.
  InsertNormalRebootRecords(boot_records, 9, start_time);
  action_runner_.CacheBootRecord(boot_records);
  EXPECT_FALSE(action_runner_.IsNormalRebootTooManyTimes());
  action_runner_.EnableNormalRebootAction();
  EXPECT_EQ(action_runner_.DryRun(mojom::ServiceName::kKiosk,
                                  mojom::ActionType::kNormalReboot),
            mojom::HeartbeatResponse::kSuccess);

  // 10 normal reboot records within 7 days window.
  InsertNormalRebootRecords(boot_records, 1, start_time);
  action_runner_.CacheBootRecord(boot_records);
  EXPECT_TRUE(action_runner_.IsNormalRebootTooManyTimes());
  EXPECT_EQ(action_runner_.DryRun(mojom::ServiceName::kKiosk,
                                  mojom::ActionType::kNormalReboot),
            mojom::HeartbeatResponse::kRateLimit);

  // If we move forward 1 second, we should be able to call the reboot action
  // again.
  task_environment_.FastForwardBy(base::Seconds(1));
  EXPECT_FALSE(action_runner_.IsNormalRebootTooManyTimes());
  EXPECT_EQ(action_runner_.DryRun(mojom::ServiceName::kKiosk,
                                  mojom::ActionType::kNormalReboot),
            mojom::HeartbeatResponse::kSuccess);
}

TEST_F(ActionRunnerTest, ForceReboot12HoursThreshold) {
  std::vector<BootRecord> boot_records;
  auto start_time = base::Time().Now();
  task_environment_.FastForwardBy(base::Hours(12));

  // 2 abnormal reboot records within 12 hours window.
  InsertAbnormalRebootRecords(boot_records, 2, start_time);
  action_runner_.CacheBootRecord(boot_records);
  EXPECT_FALSE(action_runner_.IsForceRebootTooManyTimes());

  // 3 abnormal reboot records within 12 hours window.
  InsertAbnormalRebootRecords(boot_records, 1, start_time);
  action_runner_.CacheBootRecord(boot_records);
  EXPECT_TRUE(action_runner_.IsForceRebootTooManyTimes());

  // If we move forward 1 second, we should be able to call the reboot action
  // again.
  task_environment_.FastForwardBy(base::Seconds(1));
  EXPECT_FALSE(action_runner_.IsForceRebootTooManyTimes());
}

TEST_F(ActionRunnerTest, ForceReboot7DaysThreshold) {
  std::vector<BootRecord> boot_records;
  auto start_time = base::Time().Now();
  task_environment_.FastForwardBy(base::Days(7));

  // 9 abnormal reboot records within 7 days window.
  InsertAbnormalRebootRecords(boot_records, 9, start_time);
  action_runner_.CacheBootRecord(boot_records);
  EXPECT_FALSE(action_runner_.IsForceRebootTooManyTimes());

  // 10 abnormal reboot records within 7 days window.
  InsertAbnormalRebootRecords(boot_records, 1, start_time);
  action_runner_.CacheBootRecord(boot_records);
  EXPECT_TRUE(action_runner_.IsForceRebootTooManyTimes());

  // If we move forward 1 second, we should be able to call the reboot action
  // again.
  task_environment_.FastForwardBy(base::Seconds(1));
  EXPECT_FALSE(action_runner_.IsForceRebootTooManyTimes());
}

}  // namespace

}  // namespace heartd
