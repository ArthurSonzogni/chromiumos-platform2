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
#include <featured/feature_library.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <optional>
#include <utility>
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
  PlatformFeature(const std::string& name,
                  std::vector<std::unique_ptr<FeatureCommand>>&& query_cmds,
                  std::vector<std::unique_ptr<FeatureCommand>>&& feature_cmds)
      : exec_cmds_(std::move(feature_cmds)),
        support_check_cmds_(std::move(query_cmds)),
        name_(std::make_unique<std::string>(name)),
        feature_{name_->c_str(), FEATURE_DISABLED_BY_DEFAULT} {}
  PlatformFeature(PlatformFeature&& other) = default;
  PlatformFeature(const PlatformFeature& other) = delete;
  PlatformFeature& operator=(const PlatformFeature& other) = delete;

  const std::string& name() const { return *name_; }

  // Don't copy this because address must *not* change across lookups.
  const Feature* const feature() const { return &feature_; }

  // Check if feature is supported on the device
  bool IsSupported() const;

  // Execute a sequence of commands to enable a feature
  bool Execute() const;

 private:
  std::vector<std::unique_ptr<FeatureCommand>> exec_cmds_;
  std::vector<std::unique_ptr<FeatureCommand>> support_check_cmds_;
  // The string in this unique_ptr must not be modified since feature_ contains
  // a pointer to the underlying c_str().
  std::unique_ptr<const std::string> name_;
  Feature feature_;
};

class FeatureParserBase {
 public:
  using FeatureMap = std::unordered_map<std::string, PlatformFeature>;
  virtual bool ParseFileContents(const std::string& file_contents) = 0;
  virtual ~FeatureParserBase() = default;
  bool AreFeaturesParsed() const { return features_parsed_; }
  const FeatureMap* GetFeatureMap() { return &feature_map_; }

 protected:
  std::unordered_map<std::string, PlatformFeature> feature_map_;
  // Parse features only once per object
  bool features_parsed_ = false;
};

class JsonFeatureParser : public FeatureParserBase {
 public:
  // Implements the meat of the JSON parsing functionality given a JSON blob
  bool ParseFileContents(const std::string& file_contents) override;

 private:
  // Helper to build a PlatformFeature object by parsing a JSON feature object
  std::optional<PlatformFeature> MakeFeatureObject(
      const base::Value& feature_obj);
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
  bool ParseFeatureList();

  // Enable all features that are supported and which Chrome tells us should be
  // enabled.
  bool EnableFeatures();

  // Wraps IsPlatformFeatureEnabled, providing the interface that dbus expects
  void IsPlatformFeatureEnabledWrap(
      dbus::MethodCall* method_call,
      dbus::ExportedObject::ResponseSender sender);

  bool IsPlatformFeatureEnabled(const std::string& name);

  std::unique_ptr<FeatureParserBase> parser_;
  std::unique_ptr<feature::PlatformFeatures> library_;
};

}  // namespace featured

#endif  // FEATURED_SERVICE_H_
