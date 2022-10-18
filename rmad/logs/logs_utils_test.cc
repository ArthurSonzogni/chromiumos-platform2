// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/logs/logs_utils.h"

#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/memory/scoped_refptr.h>
#include <base/values.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "rmad/logs/logs_constants.h"
#include "rmad/utils/json_store.h"

using testing::_;
using testing::Return;

namespace {

constexpr char kTestJsonStoreFilename[] = "test.json";
constexpr char kDefaultJson[] = R"(
{
  "metrics": {},
  "other": {},
  "running_time": 446.1482148170471,
  "setup_timestamp": 1663970456.867931
}
)";

}  // namespace

namespace rmad {

class LogsUtilsTest : public testing::Test {
 public:
  LogsUtilsTest() = default;

 protected:
  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    file_path_ = temp_dir_.GetPath().AppendASCII(kTestJsonStoreFilename);
  }

  bool CreateInputFile(const char* str, int size) {
    if (base::WriteFile(file_path_, str, size) == size) {
      json_store_ = base::MakeRefCounted<JsonStore>(file_path_);
      return true;
    }
    return false;
  }

  base::ScopedTempDir temp_dir_;
  scoped_refptr<JsonStore> json_store_;
  base::FilePath file_path_;
};

// Simulates adding two events to an empty `logs` json.
TEST_F(LogsUtilsTest, RecordStateTransition) {
  const RmadState::StateCase state1 = RmadState::kWelcome;
  const RmadState::StateCase state2 = RmadState::kRestock;
  const RmadState::StateCase state3 = RmadState::kRunCalibration;

  EXPECT_TRUE(CreateInputFile(kDefaultJson, std::size(kDefaultJson) - 1));

  EXPECT_TRUE(RecordStateTransitionToLogs(json_store_, state1, state2));
  base::Value logs(base::Value::Type::DICT);
  json_store_->GetValue(kLogs, &logs);

  const base::Value::List* events = logs.GetDict().FindList(kEvents);
  EXPECT_EQ(1, events->size());
  const base::Value::Dict& event1 = (*events)[0].GetDict();
  EXPECT_EQ(static_cast<int>(state1),
            event1.FindDict(kDetails)->FindInt(kFromStateId));
  EXPECT_EQ(static_cast<int>(state2),
            event1.FindDict(kDetails)->FindInt(kToStateId));

  EXPECT_TRUE(RecordStateTransitionToLogs(json_store_, state2, state3));
  json_store_->GetValue(kLogs, &logs);

  events = logs.GetDict().FindList(kEvents);
  EXPECT_EQ(2, events->size());
  const base::Value::Dict& event2 = (*events)[1].GetDict();
  EXPECT_EQ(static_cast<int>(state2),
            event2.FindDict(kDetails)->FindInt(kFromStateId));
  EXPECT_EQ(static_cast<int>(state3),
            event2.FindDict(kDetails)->FindInt(kToStateId));
}

}  // namespace rmad
