// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <spaced/calculator/stateful_free_space_calculator.h>

#include <memory>
#include <string>
#include <vector>

#include <sys/statvfs.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <base/strings/stringprintf.h>
#include <base/task/sequenced_task_runner.h>
#include "base/test/task_environment.h"
#include <base/task/thread_pool.h>
#include <brillo/blkdev_utils/mock_lvm.h>

using testing::_;
using testing::DoAll;
using testing::Return;
using testing::SetArgPointee;

namespace spaced {
namespace {
constexpr const char kSampleReport[] =
    "{\"report\": [ { \"lv\": [ {\"lv_name\":\"thinpool\", "
    "\"vg_name\":\"STATEFUL\", \"lv_size\":\"%ldB\", "
    "\"data_percent\":\"%f\"} ] } ] }";

class StatefulFreeSpaceCalculatorMock : public StatefulFreeSpaceCalculator {
 public:
  StatefulFreeSpaceCalculatorMock(
      struct statvfs st,
      scoped_refptr<base::SequencedTaskRunner> task_runner,
      int64_t time_delta_seconds,
      std::optional<brillo::Thinpool> thinpool)
      : StatefulFreeSpaceCalculator(task_runner, time_delta_seconds, thinpool),
        st_(st) {}

 protected:
  int StatVFS(const base::FilePath& path, struct statvfs* st) override {
    memcpy(st, &st_, sizeof(struct statvfs));
    return !st_.f_fsid;
  }

 private:
  struct statvfs st_;
};

}  // namespace

class StatefulFreeSpaceCalculatorTest : public testing::Test {
 public:
  StatefulFreeSpaceCalculatorTest() = default;
  ~StatefulFreeSpaceCalculatorTest() override = default;
  StatefulFreeSpaceCalculatorTest(const StatefulFreeSpaceCalculatorTest&) =
      delete;
  StatefulFreeSpaceCalculatorTest& operator=(
      const StatefulFreeSpaceCalculatorTest&) = delete;

  scoped_refptr<base::SequencedTaskRunner> GetTestThreadRunner() {
    return task_environment_.GetMainThreadTaskRunner();
  }

 private:
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::ThreadingMode::MAIN_THREAD_ONLY,
      base::test::TaskEnvironment::MainThreadType::IO};
};

TEST_F(StatefulFreeSpaceCalculatorTest, StatVfsError) {
  struct statvfs st = {};

  StatefulFreeSpaceCalculatorMock calculator(st, GetTestThreadRunner(), 0,
                                             std::nullopt);

  calculator.UpdateSize();
  EXPECT_EQ(calculator.GetSize(), -1);
}

TEST_F(StatefulFreeSpaceCalculatorTest, NoThinpoolCalculator) {
  struct statvfs st = {};
  st.f_fsid = 1;
  st.f_bavail = 1024;
  st.f_blocks = 2048;
  st.f_frsize = 4096;

  StatefulFreeSpaceCalculatorMock calculator(st, GetTestThreadRunner(), 0,
                                             std::nullopt);

  calculator.UpdateSize();
  EXPECT_EQ(calculator.GetSize(), 4194304);
}

TEST_F(StatefulFreeSpaceCalculatorTest, ThinpoolCalculator) {
  struct statvfs st = {};
  st.f_fsid = 1;
  st.f_bavail = 1024;
  st.f_blocks = 2048;
  st.f_frsize = 4096;

  auto lvm_command_runner = std::make_shared<brillo::MockLvmCommandRunner>();
  brillo::Thinpool thinpool("thinpool", "STATEFUL", lvm_command_runner);

  std::vector<std::string> cmd = {
      "/sbin/lvdisplay",  "-S",   "pool_lv=\"\"", "-C",
      "--reportformat",   "json", "--units",      "b",
      "STATEFUL/thinpool"};

  std::string report = base::StringPrintf(kSampleReport, 16777216L, 80.0);
  EXPECT_CALL(*lvm_command_runner.get(), RunProcess(cmd, _))
      .WillRepeatedly(DoAll(SetArgPointee<1>(report), Return(true)));

  StatefulFreeSpaceCalculatorMock calculator(st, GetTestThreadRunner(), 0,
                                             thinpool);

  calculator.UpdateSize();
  EXPECT_EQ(calculator.GetSize(), 3355443);
}

}  // namespace spaced
