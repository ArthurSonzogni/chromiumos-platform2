// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "featured/service.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <utility>

#include <base/json/json_reader.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/stringprintf.h>
#include <build/build_config.h>
#include <build/buildflag.h>

#include "base/files/file.h"
#include "base/files/file_enumerator.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_file.h"
#include "base/files/scoped_temp_dir.h"

namespace featured {

namespace {
constexpr char kPlatformFeaturesPath[] = "/etc/init/platform-features.json";

// JSON Helper to retrieve a string value given a string key
bool GetStringFromKey(const base::Value* obj,
                      const std::string& key,
                      std::string* value) {
  const std::string* val = obj->FindStringKey(key);
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

void PlatformFeature::AddCmd(std::unique_ptr<FeatureCommand> cmd) {
  exec_cmds_.push_back(std::move(cmd));
}

void PlatformFeature::AddQueryCmd(std::unique_ptr<FeatureCommand> cmd) {
  support_check_cmds_.push_back(std::move(cmd));
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

bool JsonFeatureParser::ParseFile(const base::FilePath& path,
                                  std::string* err_str) {
  std::string input;

  if (features_parsed_)
    return true;

  if (!ReadFileToString(path, &input)) {
    *err_str = "featured: Failed to read conf file: ";
    *err_str += kPlatformFeaturesPath;
    return false;
  }

  VLOG(1) << "JSON file contents: " << input;

  base::JSONReader::ValueWithError root =
      base::JSONReader::ReadAndReturnValueWithError(input);
  if (!root.value) {
    *err_str = "featured: Failed to parse conf file: ";
    *err_str += kPlatformFeaturesPath;
    return false;
  }

  if (!root.value->is_list() || root.value->GetList().size() == 0) {
    *err_str = "featured: features list should be non-zero size!";
    return false;
  }

  for (const auto& item : root.value->GetList()) {
    const base::Value* feature_json_obj = &item;

    if (!feature_json_obj->is_dict()) {
      *err_str = "featured: features conf not list of dicts!";
      return false;
    }

    PlatformFeature feature_obj;
    if (!MakeFeatureObject(feature_json_obj, err_str, &feature_obj)) {
      return false;
    }

    auto got = feature_map_.find(feature_obj.name());
    if (got != feature_map_.end()) {
      *err_str =
          "featured: Duplicate feature name found! : " + feature_obj.name();
      return false;
    }

    feature_map_.insert(
        std::make_pair(feature_obj.name(), std::move(feature_obj)));
  }

  features_parsed_ = true;
  return true;
}

// PlatformFeature implementation (collect and execute commands).
bool JsonFeatureParser::MakeFeatureObject(const base::Value* feature_obj,
                                          std::string* err_str,
                                          PlatformFeature* platform_feat) {
  std::string feat_name;
  if (!GetStringFromKey(feature_obj, "name", &feat_name)) {
    *err_str = "featured: features conf contains empty names";
    return false;
  }

  platform_feat->SetName(feat_name);

  // Commands for querying if device is supported
  const base::Value* support_cmd_list_obj =
      feature_obj->FindListKey("support_check_commands");

  if (!support_cmd_list_obj) {
    // Feature is assumed to be always supported, such as a kernel parameter
    // that is on all device kernels.
    platform_feat->AddQueryCmd(std::make_unique<AlwaysSupportedCommand>());
  } else {
    // A support check command was provided, add it to the feature object.
    if (!support_cmd_list_obj->is_list() ||
        support_cmd_list_obj->GetList().size() == 0) {
      *err_str = "featured: Invalid format for support_check_commands commands";
      return false;
    }

    for (const auto& item : support_cmd_list_obj->GetList()) {
      const base::Value* cmd_obj = &item;
      std::string cmd_name;

      if (!GetStringFromKey(cmd_obj, "name", &cmd_name)) {
        *err_str = "featured: Invalid/Empty command name in features config.";
        return false;
      }

      if (cmd_name == "FileExists") {
        std::string file_name;

        VLOG(1) << "featured: command is FileExists";
        if (!GetStringFromKey(cmd_obj, "file", &file_name)) {
          *err_str = "featured: JSON contains invalid command name";
          return false;
        }

        platform_feat->AddQueryCmd(
            std::make_unique<FileExistsCommand>(file_name));
      } else {
        *err_str =
            "featured: Invalid support command name in features config: ";
        *err_str += cmd_name;
        return false;
      }
    }
  }

  // Commands to execute to enable feature
  const base::Value* cmd_list_obj = feature_obj->FindListKey("commands");
  if (!cmd_list_obj || !cmd_list_obj->is_list() ||
      cmd_list_obj->GetList().size() == 0) {
    *err_str = "featured: Failed to get commands list in feature.";
    return false;
  }

  for (const auto& item : cmd_list_obj->GetList()) {
    const base::Value* cmd_obj = &item;
    std::string cmd_name;

    if (!GetStringFromKey(cmd_obj, "name", &cmd_name)) {
      *err_str = "featured: Invalid command in features config.";
      return false;
    }

    if (cmd_name == "WriteFile") {
      std::string file_name, value;

      VLOG(1) << "featured: command is WriteFile";
      if (!GetStringFromKey(cmd_obj, "file", &file_name)) {
        *err_str = "featured: JSON contains invalid command name!";
        return false;
      }

      if (!GetStringFromKey(cmd_obj, "value", &value)) {
        *err_str = "featured: JSON contains invalid command value!";
        return false;
      }
      platform_feat->AddCmd(
          std::make_unique<WriteFileCommand>(file_name, value));
    } else {
      *err_str = "featured: Invalid command name in features config: ";
      *err_str += cmd_name;
      return false;
    }
  }

  return true;
}

bool DbusFeaturedService::ParseFeatureList(std::string* err_str) {
  DCHECK(err_str);

  return parser_->ParseFile(base::FilePath(kPlatformFeaturesPath), err_str);
}

bool DbusFeaturedService::GetFeatureList(std::string* csv_list,
                                         std::string* err_str) {
  DCHECK(csv_list);
  DCHECK(err_str);

  csv_list->clear();

  if (!ParseFeatureList(err_str)) {
    return false;
  }

  bool first = true;
  for (auto& it : *(parser_->GetFeatureMap())) {
    if (!first)
      csv_list->append(",");
    else
      first = false;

    csv_list->append(it.first);
  }

  return true;
}

bool DbusFeaturedService::PlatformFeatureEnable(const std::string& name,
                                                std::string* err_str) {
  DCHECK(err_str);

  if (!ParseFeatureList(err_str)) {
    return false;
  }

  auto feature = parser_->GetFeatureMap()->find(name);
  if (feature == parser_->GetFeatureMap()->end()) {
    *err_str = "featured: Feature not found in features config!";
    return false;
  }

  auto& feature_obj = feature->second;
  if (!feature_obj.IsSupported()) {
    *err_str = "featured: device does not support feature " + name;
    return false;
  }

  if (!feature_obj.Execute()) {
    *err_str = "featured: Tried but failed to enable feature " + name;
    return false;
  }

  /* On success, return the feature name to featured for context. */
  *err_str = name;
  VLOG(1) << "featured: PlatformFeatureEnable: Feature " << name << " enabled";
  return true;
}
void DbusFeaturedService::PlatformFeatureEnableWrap(
    dbus::MethodCall* method_call,
    dbus::ExportedObject::ResponseSender sender) {
  std::string out, err_str;
  bool ret;

  dbus::MessageReader reader(method_call);
  std::string name;
  if (!reader.PopString(&name)) {
    out.append("error: missing string argument");
    ret = false;
  } else if (!PlatformFeatureEnable(name, &err_str)) {
    // If failure, assign the output string as the error message
    out.append("error:");
    out.append(err_str);
    ret = false;
  } else {
    ret = true;
  }

  std::unique_ptr<dbus::Response> response =
      dbus::Response::FromMethodCall(method_call);
  dbus::MessageWriter writer(response.get());

  writer.AppendBool(ret);
  writer.AppendString(out);

  std::move(sender).Run(std::move(response));
}

void DbusFeaturedService::PlatformFeatureList(
    dbus::MethodCall* method_call,
    dbus::ExportedObject::ResponseSender sender) {
  std::string out, csv, err_str;
  bool ret;

  // If failure, assign the output string as the error message
  if (!GetFeatureList(&csv, &err_str)) {
    out.append("error:");
    out.append(err_str);
    ret = false;
  } else {
    VLOG(1) << "featured: PlatformFeatureList: " << csv;
    out = csv;
    ret = true;
  }

  std::unique_ptr<dbus::Response> response =
      dbus::Response::FromMethodCall(method_call);
  dbus::MessageWriter writer(response.get());

  writer.AppendBool(ret);
  writer.AppendString(out);

  std::move(sender).Run(std::move(response));
}

bool DbusFeaturedService::Start(dbus::Bus* bus,
                                std::shared_ptr<DbusFeaturedService> ptr) {
  if (!bus || !bus->Connect()) {
    LOG(ERROR) << "Failed to connect to DBus";
    return false;
  }

  dbus::ObjectPath path(featured::kFeaturedServicePath);
  dbus::ExportedObject* object = bus->GetExportedObject(path);
  if (!object) {
    LOG(ERROR) << "Failed to get exported object at " << path.value();
    return false;
  }

  if (!object->ExportMethodAndBlock(
          featured::kFeaturedServiceName, featured::kPlatformFeatureList,
          base::BindRepeating(&DbusFeaturedService::PlatformFeatureList,
                              ptr))) {
    bus->UnregisterExportedObject(path);
    LOG(ERROR) << "Failed to export method " << featured::kPlatformFeatureList;
    return false;
  }

  if (!object->ExportMethodAndBlock(
          featured::kFeaturedServiceName, featured::kPlatformFeatureEnable,
          base::BindRepeating(&DbusFeaturedService::PlatformFeatureEnableWrap,
                              base::Unretained(this)))) {
    bus->UnregisterExportedObject(path);
    LOG(ERROR) << "Failed to export method "
               << featured::kPlatformFeatureEnable;
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
