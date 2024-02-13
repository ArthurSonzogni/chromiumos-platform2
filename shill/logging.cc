// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/logging.h"

#include <string>

#include <base/command_line.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/json/json_file_value_serializer.h>
#include <base/json/json_string_value_serializer.h>
#include <base/json/values_util.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/time/time.h>
#include <brillo/files/file_util.h>

#include "shill/scope_logger.h"

namespace shill {

namespace switches {

const char kLogLevel[] = "log-level";
const char kLogScopes[] = "log-scopes";

}  // namespace switches

namespace {
constexpr char kLogTime[] = "start-time";
constexpr base::TimeDelta kValidTime = base::Days(3);
}  // namespace

void SetLogLevelFromCommandLine(base::CommandLine* cl) {
  if (cl->HasSwitch(switches::kLogLevel)) {
    std::string log_level = cl->GetSwitchValueASCII(switches::kLogLevel);
    int level = 0;
    if (base::StringToInt(log_level, &level) &&
        level < logging::LOGGING_NUM_SEVERITIES) {
      logging::SetMinLogLevel(level);
      // Like VLOG, SLOG uses negative verbose level.
      shill::ScopeLogger::GetInstance()->set_verbose_level(-level);
    } else {
      LOG(WARNING) << "Bad log level: " << log_level;
    }
  }

  if (cl->HasSwitch(switches::kLogScopes)) {
    std::string log_scopes = cl->GetSwitchValueASCII(switches::kLogScopes);
    shill::ScopeLogger::GetInstance()->EnableScopesByName(log_scopes);
  }
}

bool PersistOverrideLogConfig(const base::FilePath& path, bool enabled) {
  if (!enabled) {  // remove config
    if (!brillo::DeleteFile(path)) {
      PLOG(WARNING) << "Failed to remove log override file: " << path;
      return false;
    }
    return true;
  }
  // write config
  base::Value::Dict log_config;

  std::string tags = ScopeLogger::GetInstance()->GetEnabledScopeNames();
  int level = logging::GetMinLogLevel();

  log_config.Set(switches::kLogLevel, level);
  log_config.Set(switches::kLogScopes, tags);
  log_config.Set(kLogTime, TimeToValue(base::Time::Now()));

  std::string file_content;
  JSONStringValueSerializer log_serializer(&file_content);
  if (!log_serializer.Serialize(log_config)) {
    LOG(WARNING) << "Failed to serialize the log config";
    return false;
  }

  if (!base::WriteFile(path, file_content)) {
    PLOG(WARNING) << "Failed to write log override file: " << path;
    brillo::DeleteFile(path);  // just in case of a partial write
    return false;
  }
  return true;
}

bool ApplyOverrideLogConfig(const base::FilePath& path) {
  JSONFileValueDeserializer logging_override(path);

  int error_code = 0;
  std::string error_msg;

  auto override_value = logging_override.Deserialize(&error_code, &error_msg);

  if (!override_value) {
    if (error_code != logging_override.JSON_NO_SUCH_FILE) {
      LOG(WARNING) << "Failed to parse: " << path << ", error: " << error_msg;
      brillo::DeleteFile(path);
    }
    return false;
  }
  if (!override_value->is_dict()) {
    LOG(WARNING) << "Invalid log override config: " << path;
    brillo::DeleteFile(path);
    return false;
  }

  auto& log_config = override_value->GetDict();
  auto start_time = ValueToTime(log_config.Find(kLogTime));

  if (!start_time || *start_time > base::Time::Now()) {
    LOG(WARNING) << "Missing or invalid time-stamp in: " << path;
    brillo::DeleteFile(path);
    return false;
  }
  if (*start_time + kValidTime < base::Time::Now()) {
    LOG(INFO) << "Stale log override config - using defaults";
    brillo::DeleteFile(path);
    return false;
  }

  auto level = log_config.FindInt(switches::kLogLevel);
  auto scopes = log_config.FindString(switches::kLogScopes);
  if (!level || *level >= logging::LOGGING_NUM_SEVERITIES || !scopes) {
    LOG(WARNING) << "Missing or invalid log config in: " << path;
    brillo::DeleteFile(path);
    return false;
  }

  logging::SetMinLogLevel(*level);
  // Like VLOG, SLOG uses negative verbose level.
  shill::ScopeLogger::GetInstance()->set_verbose_level(-(*level));
  shill::ScopeLogger::GetInstance()->EnableScopesByName(*scopes);

  LOG(INFO) << "Restored log configuration: " << *level << ", " << *scopes;
  return true;
}

}  // namespace shill
