// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FEATURED_SERVICE_H_
#define FEATURED_SERVICE_H_

#include "dbus/exported_object.h"
#include <dbus/object_path.h>
#include <chromeos/dbus/service_constants.h>

#include <base/command_line.h>
#include <base/macros.h>
#include <base/values.h>

#include <brillo/dbus/exported_object_manager.h>
#include <brillo/dbus/exported_property_set.h>
#include <brillo/dbus/dbus_method_response.h>
#include <brillo/errors/error.h>
#include <brillo/syslog_logging.h>
#include <brillo/variant_dictionary.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace featured {
class FeatureCommand {
 public:
  explicit FeatureCommand(const std::string& name) : name_(name) {}
  FeatureCommand(FeatureCommand&& other) = default;
  // virtual destructor is required because we create a unique pointer
  // of an abstract class. See PlatformFeature class definition.
  virtual ~FeatureCommand() = default;

  std::string name() { return name_; }
  virtual bool Execute() = 0;

 private:
  std::string name_;
};

class WriteFileCommand : public FeatureCommand {
 public:
  WriteFileCommand(const std::string& file_name, const std::string& value);
  WriteFileCommand(WriteFileCommand&& other) = default;
  bool Execute() override;

 private:
  std::string file_name_;
  std::string value_;
};

class FileExistsCommand : public FeatureCommand {
 public:
  explicit FileExistsCommand(const std::string& file_name);
  FileExistsCommand(FileExistsCommand&& other) = default;
  bool Execute() override;

 private:
  std::string file_name_;
};

class AlwaysSupportedCommand : public FeatureCommand {
 public:
  AlwaysSupportedCommand() : FeatureCommand("AlwaysSupported") {}
  AlwaysSupportedCommand(AlwaysSupportedCommand&& other) = default;
  bool Execute() override { return true; }
};

class PlatformFeature {
 public:
  PlatformFeature() = default;
  PlatformFeature(PlatformFeature&& other) = default;
  PlatformFeature(const PlatformFeature& other) = delete;
  PlatformFeature& operator=(const PlatformFeature& other) = delete;

  std::string name() { return name_; }
  void SetName(std::string name) { name_ = name; }

  // Check if feature is supported on the device
  bool IsSupported() const;

  // Execute a sequence of commands to enable a feature
  bool Execute() const;

  // Used by the parser to add commands to a feature
  void AddCmd(std::unique_ptr<FeatureCommand> cmd);
  void AddQueryCmd(std::unique_ptr<FeatureCommand> cmd);

 private:
  std::vector<std::unique_ptr<FeatureCommand>> exec_cmds_;
  std::vector<std::unique_ptr<FeatureCommand>> support_check_cmds_;
  std::string name_;
};

class FeatureParserBase {
 public:
  using FeatureMap = std::unordered_map<std::string, PlatformFeature>;
  virtual bool ParseFile(const base::FilePath& path, std::string* err_str) = 0;
  virtual ~FeatureParserBase() = default;
  const FeatureMap* GetFeatureMap() { return &feature_map_; }

 protected:
  std::unordered_map<std::string, PlatformFeature> feature_map_;
  // Parse features only once per object
  bool features_parsed_ = false;
};

class JsonFeatureParser : public FeatureParserBase {
 public:
  // Implements the meat of the JSON parsing functionality given a JSON path
  bool ParseFile(const base::FilePath& path, std::string* err_str) override;

 private:
  // Helper to build a PlatformFeature object by parsing a JSON feature object
  bool MakeFeatureObject(const base::Value* feature_obj,
                         std::string* err_str,
                         PlatformFeature* kf);
};

class DbusFeaturedService {
 public:
  DbusFeaturedService() : parser_(std::make_unique<JsonFeatureParser>()) {}
  DbusFeaturedService(const DbusFeaturedService&) = delete;
  DbusFeaturedService& operator=(const DbusFeaturedService&) = delete;

  ~DbusFeaturedService() = default;

  bool Start(dbus::Bus* bus, std::shared_ptr<DbusFeaturedService> ptr);

 private:
  // Helpers to invoke a feature parser
  bool ParseFeatureList(std::string* err_str);
  std::unique_ptr<FeatureParserBase> parser_;

  // List all the platform features supported by the platform
  bool GetFeatureList(std::string* csv_list, std::string* err_str);
  void PlatformFeatureList(dbus::MethodCall* method_call,
                           dbus::ExportedObject::ResponseSender sender);

  // Wraps PlatformFeatureEnable, providing the interface that dbus expects
  void PlatformFeatureEnableWrap(dbus::MethodCall* method_call,
                                 dbus::ExportedObject::ResponseSender sender);

  bool PlatformFeatureEnable(const std::string& name, std::string* err_str);
};

}  // namespace featured

#endif  // FEATURED_SERVICE_H_
