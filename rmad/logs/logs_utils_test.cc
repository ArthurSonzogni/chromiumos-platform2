// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/logs/logs_utils.h"

#include <string>
#include <utility>
#include <vector>

#include <base/containers/contains.h>
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

// Simulates adding replaced components to an empty `logs` json.
TEST_F(LogsUtilsTest, RecordSelectedComponents) {
  EXPECT_TRUE(CreateInputFile(kDefaultJson, std::size(kDefaultJson) - 1));

  const bool rework_selected_expected_value = true;
  const std::string audio_codec =
      RmadComponent_Name(RMAD_COMPONENT_AUDIO_CODEC);
  const std::string battery = RmadComponent_Name(RMAD_COMPONENT_BATTERY);

  EXPECT_TRUE(RecordSelectedComponentsToLogs(
      json_store_, std::vector<std::string>({audio_codec, battery}),
      rework_selected_expected_value));
  base::Value logs(base::Value::Type::DICT);
  json_store_->GetValue(kLogs, &logs);

  const base::Value::List* events = logs.GetDict().FindList(kEvents);
  EXPECT_EQ(1, events->size());
  const base::Value::Dict& event = (*events)[0].GetDict();
  EXPECT_EQ(static_cast<int>(RmadState::kComponentsRepair),
            event.FindInt(kStateId));
  EXPECT_EQ(static_cast<int>(LogEventType::kData), event.FindInt(kType));

  const base::Value::List* components =
      event.FindDict(kDetails)->FindList(kLogReplacedComponents);
  EXPECT_EQ(2, components->size());
  EXPECT_EQ(audio_codec, (*components)[0].GetString());
  EXPECT_EQ(battery, (*components)[1].GetString());
  EXPECT_TRUE(event.FindDict(kDetails)->FindBool(kLogReworkSelected).value());
}

// Simulates adding the device destination to an empty `logs` json.
TEST_F(LogsUtilsTest, RecordDeviceDestination) {
  EXPECT_TRUE(CreateInputFile(kDefaultJson, std::size(kDefaultJson) - 1));

  const std::string device_destination =
      ReturningOwner_Name(ReturningOwner::RMAD_RETURNING_OWNER_DIFFERENT_OWNER);

  EXPECT_TRUE(RecordDeviceDestinationToLogs(json_store_, device_destination));
  base::Value logs(base::Value::Type::DICT);
  json_store_->GetValue(kLogs, &logs);

  const base::Value::List* events = logs.GetDict().FindList(kEvents);
  EXPECT_EQ(1, events->size());
  const base::Value::Dict& event = (*events)[0].GetDict();
  EXPECT_EQ(static_cast<int>(RmadState::kDeviceDestination),
            event.FindInt(kStateId));
  EXPECT_EQ(static_cast<int>(LogEventType::kData), event.FindInt(kType));
  EXPECT_EQ(device_destination,
            *event.FindDict(kDetails)->FindString(kLogDestination));
}

// Simulates adding the wipe device decision to an empty `logs` json.
TEST_F(LogsUtilsTest, RecordWipeDevice) {
  EXPECT_TRUE(CreateInputFile(kDefaultJson, std::size(kDefaultJson) - 1));

  const bool wipe_device = true;

  EXPECT_TRUE(RecordWipeDeviceToLogs(json_store_, wipe_device));
  base::Value logs(base::Value::Type::DICT);
  json_store_->GetValue(kLogs, &logs);

  const base::Value::List* events = logs.GetDict().FindList(kEvents);
  EXPECT_EQ(1, events->size());
  const base::Value::Dict& event = (*events)[0].GetDict();
  EXPECT_EQ(static_cast<int>(RmadState::kWipeSelection),
            event.FindInt(kStateId));
  EXPECT_EQ(static_cast<int>(LogEventType::kData), event.FindInt(kType));
  EXPECT_TRUE(event.FindDict(kDetails)->FindBool(kLogWipeDevice).value());
}

// Simulates adding the wp disable method to an empty `logs` json.
TEST_F(LogsUtilsTest, RecordWpDisableMethod) {
  EXPECT_TRUE(CreateInputFile(kDefaultJson, std::size(kDefaultJson) - 1));

  const std::string wp_disable_method =
      WpDisableMethod_Name(RMAD_WP_DISABLE_METHOD_RSU);

  EXPECT_TRUE(RecordWpDisableMethodToLogs(json_store_, wp_disable_method));
  base::Value logs(base::Value::Type::DICT);
  json_store_->GetValue(kLogs, &logs);

  const base::Value::List* events = logs.GetDict().FindList(kEvents);
  EXPECT_EQ(1, events->size());
  const base::Value::Dict& event = (*events)[0].GetDict();
  EXPECT_EQ(static_cast<int>(RmadState::kWpDisableMethod),
            event.FindInt(kStateId));
  EXPECT_EQ(static_cast<int>(LogEventType::kData), event.FindInt(kType));
  EXPECT_EQ(wp_disable_method,
            *event.FindDict(kDetails)->FindString(kLogWpDisableMethod));
}

// Simulates adding the RSU challenge code to an empty `logs` json.
TEST_F(LogsUtilsTest, RecordRsuChallengeCode) {
  EXPECT_TRUE(CreateInputFile(kDefaultJson, std::size(kDefaultJson) - 1));

  const std::string challenge_code = "H65SFQL111PBRSB6PDIRTMFO0KHG3QZW0YSF04PW";
  const std::string hwid = "BOOK_C4B-A3F-B4U-E2U-B4E-A6T";

  EXPECT_TRUE(RecordRsuChallengeCodeToLogs(json_store_, challenge_code, hwid));
  base::Value logs(base::Value::Type::DICT);
  json_store_->GetValue(kLogs, &logs);

  const base::Value::List* events = logs.GetDict().FindList(kEvents);
  EXPECT_EQ(1, events->size());
  const base::Value::Dict& event = (*events)[0].GetDict();
  EXPECT_EQ(static_cast<int>(RmadState::kWpDisableRsu),
            event.FindInt(kStateId));
  EXPECT_EQ(static_cast<int>(LogEventType::kData), event.FindInt(kType));
  EXPECT_EQ(challenge_code,
            *event.FindDict(kDetails)->FindString(kLogRsuChallengeCode));
  EXPECT_EQ(hwid, *event.FindDict(kDetails)->FindString(kLogRsuHwid));
}

}  // namespace rmad
