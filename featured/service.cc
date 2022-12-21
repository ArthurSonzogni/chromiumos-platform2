// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

#include <base/files/file.h>
#include <base/files/file_enumerator.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/files/scoped_temp_dir.h>
#include <base/json/json_reader.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/stringprintf.h>
#include <build/build_config.h>
#include <build/buildflag.h>

#include "featured/service.h"

namespace featured {

namespace {
constexpr char kPlatformFeaturesPath[] = "/etc/featured/platform-features.json";
constexpr char kSessionStartedState[] = "started";

// Allow featured do write operations to path with these prefixes only.
std::vector<std::string> allowedPrefixes = {"/proc", "/sys"};

bool CheckPathPrefix(const base::FilePath& path) {
  bool valid = false;

  for (std::string& prefix_str : allowedPrefixes) {
    if (base::FilePath(prefix_str).IsParent(path)) {
      valid = true;
      break;
    }
  }

  return valid;
}
}  // namespace

WriteFileCommand::WriteFileCommand(const std::string& file_name,
                                   const std::string& value)
    : FeatureCommand("WriteFile") {
  file_name_ = file_name;
  value_ = value;
}

bool WriteFileCommand::Execute() {
  base::FilePath fpath = base::FilePath(file_name_);
  if (!CheckPathPrefix(fpath)) {
    PLOG(ERROR) << "Unable to write to path, prefix check fail: " << file_name_;
    PLOG(ERROR) << "Make sure to update the prefix list in sources and the "
                   "selinux config.";
    return false;
  }

  if (!base::WriteFile(fpath, value_)) {
    PLOG(ERROR) << "Unable to write to " << file_name_;
    return false;
  }
  return true;
}

MkdirCommand::MkdirCommand(const std::string& path) : FeatureCommand("Mkdir") {
  path_ = base::FilePath(path);
}

bool MkdirCommand::Execute() {
  if (!CheckPathPrefix(path_)) {
    PLOG(ERROR) << "Unable to create directory, prefix check fail: " << path_;
    return false;
  }

  if (base::PathExists(path_)) {
    PLOG(ERROR) << "Path already exists, cannot create directory: " << path_;
    return false;
  }

  if (!base::CreateDirectory(path_)) {
    PLOG(ERROR) << "Unable to create directory: " << path_;
    return false;
  }
  return true;
}

FileExistsCommand::FileExistsCommand(const std::string& file_name)
    : FeatureCommand("FileExists") {
  file_name_ = file_name;
}

bool FileExistsCommand::Execute() {
  return base::PathExists(base::FilePath(file_name_));
}

FileNotExistsCommand::FileNotExistsCommand(const std::string& file_name)
    : FeatureCommand("FileNotExists") {
  file_name_ = file_name;
}

bool FileNotExistsCommand::Execute() {
  return !base::PathExists(base::FilePath(file_name_));
}

bool PlatformFeature::Execute() const {
  for (auto& cmd : exec_cmds_) {
    if (!cmd->Execute()) {
      LOG(ERROR) << "Failed to execute command: " << cmd->name();
      return false;
    }
  }
  return true;
}

bool PlatformFeature::IsSupported() const {
  for (auto& cmd : support_check_cmds_) {
    if (!cmd->Execute()) {
      return false;
    }
  }
  return true;
}

bool JsonFeatureParser::ParseFileContents(const std::string& file_contents) {
  if (features_parsed_)
    return true;

  VLOG(1) << "JSON file contents: " << file_contents;

  auto root = base::JSONReader::ReadAndReturnValueWithError(file_contents);
  if (!root.has_value()) {
    LOG(ERROR) << "Failed to parse conf file: " << kPlatformFeaturesPath;
    return false;
  }

  if (!root->is_list() || root->GetList().size() == 0) {
    LOG(ERROR) << "features list should be non-zero size!";
    return false;
  }

  for (const auto& item : root->GetList()) {
    if (!item.is_dict()) {
      LOG(ERROR) << "features conf not list of dicts!";
      return false;
    }

    auto feature_obj_optional = MakeFeatureObject(item.GetDict());
    if (!feature_obj_optional) {
      return false;
    }
    PlatformFeature feature_obj(std::move(*feature_obj_optional));

    auto got = feature_map_.find(feature_obj.name());
    if (got != feature_map_.end()) {
      LOG(ERROR) << "Duplicate feature name found: " << feature_obj.name();
      return false;
    }

    feature_map_.insert(
        std::make_pair(feature_obj.name(), std::move(feature_obj)));
  }

  features_parsed_ = true;
  return true;
}

// PlatformFeature implementation (collect and execute commands).
std::optional<PlatformFeature> JsonFeatureParser::MakeFeatureObject(
    const base::Value::Dict& feature_obj) {
  const std::string* feat_name = feature_obj.FindString("name");
  if (!feat_name) {
    LOG(ERROR) << "features conf contains empty names";
    return std::nullopt;
  }

  // Commands for querying if device is supported
  const base::Value::List* support_cmd_list_obj =
      feature_obj.FindList("support_check_commands");

  std::vector<std::unique_ptr<FeatureCommand>> query_cmds;
  if (!support_cmd_list_obj) {
    // Feature is assumed to be always supported, such as a kernel parameter
    // that is on all device kernels.
    query_cmds.push_back(std::make_unique<AlwaysSupportedCommand>());
  } else {
    // A support check command was provided, add it to the feature object.
    if (support_cmd_list_obj->size() == 0) {
      LOG(ERROR) << "Invalid format for support_check_commands commands";
      return std::nullopt;
    }

    for (const auto& item : *support_cmd_list_obj) {
      if (!item.is_dict()) {
        LOG(ERROR) << "support_check_commands is not list of dicts.";
        return std::nullopt;
      }

      const std::string* cmd_name = item.GetDict().FindString("name");

      if (!cmd_name) {
        LOG(ERROR) << "Invalid/Empty command name in features config.";
        return std::nullopt;
      }

      if (*cmd_name == "FileExists" || *cmd_name == "FileNotExists") {
        const std::string* file_name = item.GetDict().FindString("path");

        VLOG(1) << "featured: command is " << *cmd_name;
        if (!file_name) {
          LOG(ERROR) << "JSON contains invalid path!";
          return std::nullopt;
        }

        std::unique_ptr<FeatureCommand> cmd;
        if (*cmd_name == "FileExists") {
          cmd = std::make_unique<FileExistsCommand>(*file_name);
        } else {
          cmd = std::make_unique<FileNotExistsCommand>(*file_name);
        }

        query_cmds.push_back(std::move(cmd));
      } else {
        LOG(ERROR) << "Invalid support command name in features config: "
                   << *cmd_name;
        return std::nullopt;
      }
    }
  }

  // Commands to execute to enable feature
  const base::Value::List* cmd_list_obj = feature_obj.FindList("commands");
  if (!cmd_list_obj || cmd_list_obj->size() == 0) {
    LOG(ERROR) << "Failed to get commands list in feature.";
    return std::nullopt;
  }

  std::vector<std::unique_ptr<FeatureCommand>> feature_cmds;
  for (const auto& item : *cmd_list_obj) {
    if (!item.is_dict()) {
      LOG(ERROR) << "Invalid command in features config.";
      return std::nullopt;
    }
    const std::string* cmd_name = item.GetDict().FindString("name");

    if (!cmd_name) {
      LOG(ERROR) << "Invalid command in features config.";
      return std::nullopt;
    }

    if (*cmd_name == "WriteFile") {
      const std::string* file_name = item.GetDict().FindString("path");

      VLOG(1) << "featured: command is WriteFile";
      if (!file_name) {
        LOG(ERROR) << "JSON contains invalid path!";
        return std::nullopt;
      }

      const std::string* value = item.GetDict().FindString("value");
      if (!value) {
        LOG(ERROR) << "JSON contains invalid command value!";
        return std::nullopt;
      }
      feature_cmds.push_back(
          std::make_unique<WriteFileCommand>(*file_name, *value));
    } else if (*cmd_name == "Mkdir") {
      const std::string* file_name = item.GetDict().FindString("path");

      VLOG(1) << "featured: command is Mkdir";
      if (!file_name) {
        LOG(ERROR) << "JSON contains invalid path!";
        return std::nullopt;
      }

      feature_cmds.push_back(std::make_unique<MkdirCommand>(*file_name));
    } else {
      LOG(ERROR) << "Invalid command name in features config: " << *cmd_name;
      return std::nullopt;
    }
  }

  return PlatformFeature(*feat_name, std::move(query_cmds),
                         std::move(feature_cmds));
}

bool DbusFeaturedService::ParseFeatureList() {
  if (parser_->AreFeaturesParsed())
    return true;

  std::string file_contents;
  if (!ReadFileToString(base::FilePath(kPlatformFeaturesPath),
                        &file_contents)) {
    PLOG(ERROR) << "Failed to read conf file: " << kPlatformFeaturesPath;
    return false;
  }

  return parser_->ParseFileContents(file_contents);
}

bool DbusFeaturedService::EnableFeatures() {
  if (!ParseFeatureList()) {
    return false;
  }
  for (const auto& it : *(parser_->GetFeatureMap())) {
    if (it.second.IsSupported() &&
        library_->IsEnabledBlocking(*it.second.feature())) {
      it.second.Execute();
    }
  }
  return true;
}

void DbusFeaturedService::OnSessionStateChanged(const std::string& state) {
  if (state == kSessionStartedState && !evaluated_platform_features_json_) {
    if (!EnableFeatures()) {
      LOG(ERROR) << "failed to enable features";
      return;
    }
    evaluated_platform_features_json_ = true;
  }
}

void OnSignalConnected(const std::string& interface,
                       const std::string& signal,
                       bool success) {
  if (!success) {
    LOG(ERROR) << "Could not connect to signal " << signal << " on interface "
               << interface;
  }
}

bool DbusFeaturedService::Start(dbus::Bus* bus,
                                std::shared_ptr<DbusFeaturedService> ptr) {
  if (!bus || !bus->Connect()) {
    LOG(ERROR) << "Failed to connect to DBus";
    return false;
  }

  library_ = feature::PlatformFeatures::New(bus);

  dbus::ObjectPath path(featured::kFeaturedServicePath);
  dbus::ExportedObject* object = bus->GetExportedObject(path);
  if (!object) {
    LOG(ERROR) << "Failed to get exported object at " << path.value();
    return false;
  }

  // Late boot features do not expect to start until after login, so delay them
  // until then.
  session_manager_.reset(new org::chromium::SessionManagerInterfaceProxy(bus));
  session_manager_->RegisterSessionStateChangedSignalHandler(
      base::BindRepeating(&DbusFeaturedService::OnSessionStateChanged,
                          weak_ptr_factory_.GetWeakPtr()),
      base::BindOnce(&OnSignalConnected));

  if (!bus->RequestOwnershipAndBlock(featured::kFeaturedServiceName,
                                     dbus::Bus::REQUIRE_PRIMARY)) {
    bus->UnregisterExportedObject(path);
    LOG(ERROR) << "Failed to get ownership of "
               << featured::kFeaturedServiceName;
    return false;
  }

  return true;
}

}  // namespace featured
