// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "featured/service.h"

#include <memory>
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

namespace featured {

namespace {
constexpr char kPlatformFeaturesPath[] = "/etc/init/platform-features.json";

// JSON Helper to retrieve a string value given a string key
bool GetStringFromKey(const base::Value& obj,
                      const std::string& key,
                      std::string* value) {
  const std::string* val = obj.FindStringKey(key);
  if (!val || val->empty()) {
    return false;
  }

  *value = *val;
  return true;
}
}  // namespace

WriteFileCommand::WriteFileCommand(const std::string& file_name,
                                   const std::string& value)
    : FeatureCommand("WriteFile") {
  file_name_ = file_name;
  value_ = value;
}

bool WriteFileCommand::Execute() {
  if (!base::WriteFile(base::FilePath(file_name_), value_)) {
    PLOG(ERROR) << "Unable to write to " << file_name_;
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

  base::JSONReader::ValueWithError root =
      base::JSONReader::ReadAndReturnValueWithError(file_contents);
  if (!root.value) {
    LOG(ERROR) << "Failed to parse conf file: " << kPlatformFeaturesPath;
    return false;
  }

  if (!root.value->is_list() || root.value->GetList().size() == 0) {
    LOG(ERROR) << "features list should be non-zero size!";
    return false;
  }

  for (const auto& item : root.value->GetList()) {
    if (!item.is_dict()) {
      LOG(ERROR) << "features conf not list of dicts!";
      return false;
    }

    auto feature_obj_optional = MakeFeatureObject(item);
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
    const base::Value& feature_obj) {
  std::string feat_name;
  if (!GetStringFromKey(feature_obj, "name", &feat_name)) {
    LOG(ERROR) << "features conf contains empty names";
    return std::nullopt;
  }

  // Commands for querying if device is supported
  const base::Value* support_cmd_list_obj =
      feature_obj.FindListKey("support_check_commands");

  std::vector<std::unique_ptr<FeatureCommand>> query_cmds;
  if (!support_cmd_list_obj) {
    // Feature is assumed to be always supported, such as a kernel parameter
    // that is on all device kernels.
    query_cmds.push_back(std::make_unique<AlwaysSupportedCommand>());
  } else {
    // A support check command was provided, add it to the feature object.
    if (!support_cmd_list_obj->is_list() ||
        support_cmd_list_obj->GetList().size() == 0) {
      LOG(ERROR) << "Invalid format for support_check_commands commands";
      return std::nullopt;
    }

    for (const auto& item : support_cmd_list_obj->GetList()) {
      if (!item.is_dict()) {
        LOG(ERROR) << "support_check_commands is not list of dicts.";
        return std::nullopt;
      }

      std::string cmd_name;

      if (!GetStringFromKey(item, "name", &cmd_name)) {
        LOG(ERROR) << "Invalid/Empty command name in features config.";
        return std::nullopt;
      }

      if (cmd_name == "FileExists") {
        std::string file_name;

        VLOG(1) << "featured: command is FileExists";
        if (!GetStringFromKey(item, "file", &file_name)) {
          LOG(ERROR) << "JSON contains invalid command name";
          return std::nullopt;
        }

        query_cmds.push_back(std::make_unique<FileExistsCommand>(file_name));
      } else {
        LOG(ERROR) << "Invalid support command name in features config: "
                   << cmd_name;
        return std::nullopt;
      }
    }
  }

  // Commands to execute to enable feature
  const base::Value* cmd_list_obj = feature_obj.FindListKey("commands");
  if (!cmd_list_obj || !cmd_list_obj->is_list() ||
      cmd_list_obj->GetList().size() == 0) {
    LOG(ERROR) << "Failed to get commands list in feature.";
    return std::nullopt;
  }

  std::vector<std::unique_ptr<FeatureCommand>> feature_cmds;
  for (const auto& item : cmd_list_obj->GetList()) {
    if (!item.is_dict()) {
      LOG(ERROR) << "Invalid command in features config.";
      return std::nullopt;
    }
    std::string cmd_name;

    if (!GetStringFromKey(item, "name", &cmd_name)) {
      LOG(ERROR) << "Invalid command in features config.";
      return std::nullopt;
    }

    if (cmd_name == "WriteFile") {
      std::string file_name, value;

      VLOG(1) << "featured: command is WriteFile";
      if (!GetStringFromKey(item, "file", &file_name)) {
        LOG(ERROR) << "JSON contains invalid command name!";
        return std::nullopt;
      }

      if (!GetStringFromKey(item, "value", &value)) {
        LOG(ERROR) << "JSON contains invalid command value!";
        return std::nullopt;
      }
      feature_cmds.push_back(
          std::make_unique<WriteFileCommand>(file_name, value));
    } else {
      LOG(ERROR) << "Invalid command name in features config: " << cmd_name;
      return std::nullopt;
    }
  }

  return PlatformFeature(feat_name, std::move(query_cmds),
                         std::move(feature_cmds));
}

bool DbusFeaturedService::ParseFeatureList() {
  if (parser_->AreFeaturesParsed())
    return true;

  std::string file_contents;
  if (!ReadFileToString(base::FilePath(kPlatformFeaturesPath),
                        &file_contents)) {
    LOG(ERROR) << "Failed to read conf file: " << kPlatformFeaturesPath;
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

bool DbusFeaturedService::IsPlatformFeatureEnabled(const std::string& name) {
  if (!ParseFeatureList()) {
    return false;
  }

  auto feature = parser_->GetFeatureMap()->find(name);
  if (feature == parser_->GetFeatureMap()->end()) {
    LOG(ERROR) << "Feature not found in features config!";
    return false;
  }

  const PlatformFeature& feature_obj = feature->second;
  if (!feature_obj.IsSupported()) {
    VLOG(1) << "device does not support feature " << name;
    return false;
  }

  bool ret = library_->IsEnabledBlocking(*feature_obj.feature());

  VLOG(1) << "featured: IsPlatformFeatureEnabled: Feature " << name
          << " enabled? " << ret;
  return ret;
}

void DbusFeaturedService::IsPlatformFeatureEnabledWrap(
    dbus::MethodCall* method_call,
    dbus::ExportedObject::ResponseSender sender) {
  bool ret;

  dbus::MessageReader reader(method_call);
  std::string name;
  if (!reader.PopString(&name)) {
    LOG(ERROR) << "missing string argument to IsPlatformFeatureEnabled";
    ret = false;
  } else {
    ret = IsPlatformFeatureEnabled(name);
  }

  std::unique_ptr<dbus::Response> response =
      dbus::Response::FromMethodCall(method_call);
  dbus::MessageWriter writer(response.get());

  writer.AppendBool(ret);

  std::move(sender).Run(std::move(response));
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

  if (!EnableFeatures()) {
    LOG(ERROR) << "Failed to enable features";
    return false;
  }

  if (!object->ExportMethodAndBlock(
          featured::kFeaturedServiceName, featured::kIsPlatformFeatureEnabled,
          base::BindRepeating(
              &DbusFeaturedService::IsPlatformFeatureEnabledWrap, ptr))) {
    bus->UnregisterExportedObject(path);
    LOG(ERROR) << "Failed to export method "
               << featured::kIsPlatformFeatureEnabled;
    return false;
  }

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
