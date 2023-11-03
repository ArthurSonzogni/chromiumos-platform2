// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FEATURED_SERVICE_H_
#define FEATURED_SERVICE_H_

#include "dbus/exported_object.h"
#include <dbus/object_path.h>
#include <chromeos/dbus/service_constants.h>

#include <base/command_line.h>
#include <base/files/file_util.h>
#include <base/memory/weak_ptr.h>
#include <base/values.h>
#include <brillo/compression/compressor_interface.h>
#include <brillo/dbus/exported_object_manager.h>
#include <brillo/dbus/exported_property_set.h>
#include <brillo/dbus/dbus_method_response.h>
#include <brillo/errors/error.h>
#include <brillo/syslog_logging.h>
#include <brillo/variant_dictionary.h>
#include <featured/feature_library.h>
#include <featured/proto_bindings/featured.pb.h>
#include <session_manager/dbus-proxies.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <optional>
#include <utility>
#include <vector>

#include "featured/store_interface.h"
#include "featured/tmp_storage_interface.h"

namespace featured {

// FeatureCommand is the base class for all commands to enable a feature.
class FeatureCommand {
 public:
  explicit FeatureCommand(const std::string& name) : name_(name) {}
  FeatureCommand(FeatureCommand&& other) = default;
  // virtual destructor is required because we create a unique pointer
  // of an abstract class. See PlatformFeature class definition.
  virtual ~FeatureCommand() = default;

  std::string name() const { return name_; }

  // Run the command to enable the feature, returning true on success.
  virtual bool Execute() = 0;

 private:
  std::string name_;
};

// Write a specified value to a specified path.
class WriteFileCommand : public FeatureCommand {
 public:
  WriteFileCommand(const std::string& file_name, const std::string& value);
  WriteFileCommand(WriteFileCommand&& other) = default;

  // Attempt to write the file, returning true on success.
  bool Execute() override;

  void SetPrefixForTesting(const base::FilePath& prefix) { prefix_ = prefix; }

 private:
  std::string file_name_;
  std::string value_;
  base::FilePath prefix_;
};

// Create a directory at a specified path, and all parent directories.
class MkdirCommand : public FeatureCommand {
 public:
  explicit MkdirCommand(const std::string& path);
  MkdirCommand(MkdirCommand&& other) = default;

  // Attempt to make the directory, returning true on success.
  bool Execute() override;

  void SetPrefixForTesting(const base::FilePath& prefix) { prefix_ = prefix; }

 private:
  base::FilePath path_;
  base::FilePath prefix_;
};

// SupportCheckCommand is the base class for all commands to check whether a
// feature is supported.
class SupportCheckCommand {
 public:
  explicit SupportCheckCommand(const std::string& name) : name_(name) {}
  SupportCheckCommand(SupportCheckCommand&& other) = default;
  // virtual destructor is required because we create a unique pointer
  // of an abstract class. See PlatformFeature class definition.
  virtual ~SupportCheckCommand() = default;

  std::string name() const { return name_; }

  // Return true if the feature is supported on this device. (false otherwise)
  virtual bool IsSupported() = 0;

 private:
  std::string name_;
};

// Mark the device as supported if a file at a given path exists.
class FileExistsCommand : public SupportCheckCommand {
 public:
  explicit FileExistsCommand(const std::string& file_name);
  FileExistsCommand(FileExistsCommand&& other) = default;
  bool IsSupported() override;

 private:
  std::string file_name_;
};

// Mark the device as supported if a file at a given path *does not* exist.
class FileNotExistsCommand : public SupportCheckCommand {
 public:
  explicit FileNotExistsCommand(const std::string& file_name);
  FileNotExistsCommand(FileNotExistsCommand&& other) = default;
  bool IsSupported() override;

 private:
  std::string file_name_;
};

// Trivial support check command that always returns true.
class AlwaysSupportedCommand : public SupportCheckCommand {
 public:
  AlwaysSupportedCommand() : SupportCheckCommand("AlwaysSupported") {}
  AlwaysSupportedCommand(AlwaysSupportedCommand&& other) = default;
  bool IsSupported() override { return true; }
};

class PlatformFeature {
 public:
  PlatformFeature(
      const std::string& name,
      std::vector<std::unique_ptr<SupportCheckCommand>>&& query_cmds,
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
  const VariationsFeature* const feature() const { return &feature_; }

  // Check if feature is supported on the device
  bool IsSupported() const;

  // Execute a sequence of commands to enable a feature
  bool Execute() const;

  // Get the names of the exec commands. Used for testing.
  std::vector<std::string> ExecCommandNamesForTesting() const;

  // Get the names of the support check commands. Used for testing.
  std::vector<std::string> SupportCheckCommandNamesForTesting() const;

 private:
  std::vector<std::unique_ptr<FeatureCommand>> exec_cmds_;
  std::vector<std::unique_ptr<SupportCheckCommand>> support_check_cmds_;
  // The string in this unique_ptr must not be modified since feature_ contains
  // a pointer to the underlying c_str().
  std::unique_ptr<const std::string> name_;
  VariationsFeature feature_;
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
      const base::Value::Dict& feature_obj);
};

class DbusFeaturedService {
 public:
  explicit DbusFeaturedService(
      std::unique_ptr<StoreInterface> store,
      std::unique_ptr<TmpStorageInterface> tmp_storage,
      std::unique_ptr<brillo::CompressorInterface> decompressor)
      : parser_(std::make_unique<JsonFeatureParser>()),
        store_(std::move(store)),
        tmp_storage_(std::move(tmp_storage)),
        decompressor_(std::move(decompressor)) {}
  DbusFeaturedService(const DbusFeaturedService&) = delete;
  DbusFeaturedService& operator=(const DbusFeaturedService&) = delete;

  ~DbusFeaturedService() = default;

  bool Start(dbus::Bus* bus, std::shared_ptr<DbusFeaturedService> ptr);

 private:
  friend class DbusFeaturedServiceTestBase;

  // Helpers to invoke a feature parser
  bool ParseFeatureList();

  // Enable all features that are supported and which Chrome tells us should be
  // enabled.
  bool EnableFeatures();

  void OnSessionStateChanged(const std::string& state);

  // Compares two SeedDetails::b64_compressed_data proto fields for equality.
  bool CompressedDataEquals(const std::string& a, const std::string& b);

  // Compares two SeedDetails protos for equality.
  bool SeedsEqual(const SeedDetails& a, const SeedDetails& b);

  // Save fetched finch seed from Chrome to disk.
  void HandleSeedFetched(dbus::MethodCall* method_call,
                         dbus::ExportedObject::ResponseSender sender);

  std::unique_ptr<FeatureParserBase> parser_;
  std::unique_ptr<StoreInterface> store_;
  std::unique_ptr<TmpStorageInterface> tmp_storage_;
  // To decompress the safe seed sent from Chrome.
  std::unique_ptr<brillo::CompressorInterface> decompressor_;
  std::unique_ptr<org::chromium::SessionManagerInterfaceProxyInterface>
      session_manager_ = nullptr;
  bool evaluated_platform_features_json_ = false;

  base::WeakPtrFactory<DbusFeaturedService> weak_ptr_factory_{this};
};

}  // namespace featured

#endif  // FEATURED_SERVICE_H_
