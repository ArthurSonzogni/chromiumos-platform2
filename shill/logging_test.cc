// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include <base/containers/contains.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/json/json_string_value_serializer.h>
#include <base/json/values_util.h>
#include <base/rand_util.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>

#include "shill/logging.h"
#include "shill/scope_logger.h"

#include <gtest/gtest.h>

namespace shill {

class LoggingTest : public testing::Test {
 protected:
  LoggingTest() = default;

  void SetUp() override {
    logger_ = ScopeLogger::GetInstance();
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    path_ = temp_dir_.GetPath().Append(shill::kLogOverrideFile);
    initial_level_ = logging::GetMinLogLevel();
    ASSERT_EQ(-initial_level_, logger_->verbose_level());
  }

  void TearDown() override {
    logger_->EnableScopesByName(initial_scopes_);
    logging::SetMinLogLevel(initial_level_);
    logger_->set_verbose_level(-initial_level_);
  }

  void ResetDefaultLogging() {
    logger_->EnableScopesByName({});
    logging::SetMinLogLevel(0);
    logger_->set_verbose_level(0);
  }

  void ExpectDefaulLogging() {
    EXPECT_TRUE(logger_->GetEnabledScopeNames().empty());
    EXPECT_EQ(logging::GetMinLogLevel(), 0);
    EXPECT_EQ(logger_->verbose_level(), 0);
  }

  void TestInvalidInput(const std::string& content) {
    EXPECT_TRUE(base::WriteFile(path_, content));
    EXPECT_FALSE(ApplyOverrideLogConfig(path_));
    EXPECT_FALSE(base::PathExists(path_));
  }

  ScopeLogger* logger_;
  base::ScopedTempDir temp_dir_;
  base::FilePath path_;
  int initial_level_;
  std::string initial_scopes_;
};

TEST_F(LoggingTest, OverrideLogConfig) {
  int level = -3;
  std::string scopes{"device+service+wifi"};
  logger_->EnableScopesByName(scopes);
  logging::SetMinLogLevel(level);
  logger_->set_verbose_level(-level);

  auto enabled_scopes = logger_->GetEnabledScopeNames();
  EXPECT_FALSE(enabled_scopes.empty());

  EXPECT_TRUE(PersistOverrideLogConfig(path_, /*enabled=*/true));

  // Reset logging and try to restore from config file.
  ResetDefaultLogging();
  EXPECT_TRUE(ApplyOverrideLogConfig(path_));
  EXPECT_EQ(enabled_scopes, logger_->GetEnabledScopeNames());
  EXPECT_EQ(level, logging::GetMinLogLevel());
  EXPECT_EQ(-level, logger_->verbose_level());

  // Now reset logging defaults, remove the log config file and try to restore.
  ResetDefaultLogging();
  EXPECT_TRUE(PersistOverrideLogConfig(path_, /*enabled=*/false));
  EXPECT_FALSE(base::PathExists(path_));
  EXPECT_FALSE(ApplyOverrideLogConfig(path_));
  ExpectDefaulLogging();
}

TEST_F(LoggingTest, OverrideLogConfig_InvalidJson) {
  std::string file_content = "an+invalid+json+format";
  TestInvalidInput(file_content);
}

TEST_F(LoggingTest, OverrideLogConfig_WrongType) {
  std::string file_content = R"(["not", "a", "dictionary"])";
  TestInvalidInput(file_content);
}

TEST_F(LoggingTest, OverrideLogConfig_NoTime) {
  std::string file_content;
  JSONStringValueSerializer json(&file_content);
  base::Value::Dict log_config;

  log_config.Set("log-level", 1);
  log_config.Set("log-scopes", "device");
  json.Serialize(log_config);
  TestInvalidInput(file_content);
}

TEST_F(LoggingTest, OverrideLogConfig_InvalidTime) {
  std::string file_content;
  JSONStringValueSerializer json(&file_content);
  base::Value::Dict log_config;

  log_config.Set("log-level", -2);
  log_config.Set("log-scopes", "wifi");
  log_config.Set("start-time", "garbage");
  json.Serialize(log_config);
  TestInvalidInput(file_content);
}

TEST_F(LoggingTest, OverrideLogConfig_TooOld) {
  std::string file_content;
  JSONStringValueSerializer json(&file_content);
  base::Value::Dict log_config;
  log_config.Set("log-level", -3);
  log_config.Set("log-scopes", "service");
  // Valid time stamp but older than 3 days.
  auto start = base::Time::Now() - base::Days(4);
  log_config.Set("start-time", TimeToValue(start));
  json.Serialize(log_config);
  TestInvalidInput(file_content);
}

TEST_F(LoggingTest, OverrideLogConfig_TooNew) {
  std::string file_content;
  JSONStringValueSerializer json(&file_content);
  base::Value::Dict log_config;
  log_config.Set("log-level", -3);
  log_config.Set("log-scopes", "service");
  // Valid time stamp but in the future.
  auto start = base::Time::Now() + base::Hours(2);
  log_config.Set("start-time", TimeToValue(start));
  json.Serialize(log_config);
  TestInvalidInput(file_content);
}

TEST_F(LoggingTest, OverrideLogConfig_NoLevel) {
  std::string file_content;
  JSONStringValueSerializer json(&file_content);
  base::Value::Dict log_config;
  auto start = base::Time::Now();
  log_config.Set("start-time", TimeToValue(start));
  log_config.Set("log-scopes", "wifi");
  json.Serialize(log_config);
  TestInvalidInput(file_content);
}

TEST_F(LoggingTest, OverrideLogConfig_NoScopes) {
  std::string file_content;
  JSONStringValueSerializer json(&file_content);
  base::Value::Dict log_config;
  auto start = base::Time::Now();
  log_config.Set("start-time", TimeToValue(start));
  log_config.Set("log-level", -1);
  json.Serialize(log_config);
  TestInvalidInput(file_content);
}

TEST_F(LoggingTest, OverrideLogConfig_InvalidLevel) {
  std::string file_content;
  JSONStringValueSerializer json(&file_content);
  base::Value::Dict log_config;
  auto start = base::Time::Now();
  log_config.Set("start-time", TimeToValue(start));
  log_config.Set("log-scopes", "wifi");
  log_config.Set("log-level", "failure");
  json.Serialize(log_config);
  TestInvalidInput(file_content);
}

TEST_F(LoggingTest, OverrideLogConfig_InvalidScopes) {
  std::string file_content;
  JSONStringValueSerializer json(&file_content);
  base::Value::Dict log_config;
  auto start = base::Time::Now();
  log_config.Set("start-time", TimeToValue(start));
  log_config.Set("log-level", -2);
  log_config.Set("log-scopes", 3.14);
  json.Serialize(log_config);
  TestInvalidInput(file_content);
}

}  // namespace shill
