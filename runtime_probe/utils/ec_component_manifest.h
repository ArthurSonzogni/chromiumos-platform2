// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RUNTIME_PROBE_UTILS_EC_COMPONENT_MANIFEST_H_
#define RUNTIME_PROBE_UTILS_EC_COMPONENT_MANIFEST_H_

#include <optional>
#include <string>
#include <vector>

#include <base/values.h>

namespace base {
class FilePath;
}  // namespace base

namespace runtime_probe {

inline constexpr char kCrosConfigImageNamePath[] = "/firmware";
inline constexpr char kCrosConfigImageNameKey[] = "image-name";
inline constexpr char kCmePath[] = "usr/share/cme/";
inline constexpr char kEcComponentManifestName[] = "component_manifest.json";

// This class handles the component manifest.
struct EcComponentManifest {
  struct Component {
    struct I2c {
      struct Expect {
        uint32_t reg;
        uint32_t value;
        static std::optional<Expect> Create(const base::Value::Dict&);
      };
      uint8_t port;
      uint8_t addr;  // 7-bit I2C address.
      std::vector<Expect> expect;

      static std::optional<I2c> Create(const base::Value::Dict&);
    };

    std::string component_type;
    std::string component_name;
    I2c i2c;

    static std::optional<Component> Create(const base::Value::Dict&);
  };

  int manifest_version;
  std::string ec_version;
  std::vector<Component> component_list;

  static std::optional<EcComponentManifest> Create(const base::Value::Dict&);
};

// This class contains only static methods to read and parse a component
// manifest file to an EcComponentManifest instance.
class EcComponentManifestReader {
 public:
  EcComponentManifestReader() = delete;
  EcComponentManifestReader(const EcComponentManifestReader&) = delete;
  EcComponentManifestReader& operator=(const EcComponentManifestReader&) =
      delete;

  // Reads and parses the component manifest from default path, returning a
  // EcComponentManifest.
  // If the content is not a valid manifest, returns std::nullopt.
  static std::optional<EcComponentManifest> Read();

  // Returns the default path to the component manifest file. This should be
  // `/usr/share/cme/<name>/component_manifest.json` where `name` can be
  // obtained by `cros_config /firmware image-name`.
  static base::FilePath EcComponentManifestDefaultPath();

 private:
  // Reads and parses the component manifest from the given path, returning a
  // EcComponentManifest.
  // If the content is not a valid manifest, returns std::nullopt.
  static std::optional<EcComponentManifest> ReadFromFilePath(
      const base::FilePath&);
};

}  // namespace runtime_probe

#endif  // RUNTIME_PROBE_UTILS_EC_COMPONENT_MANIFEST_H_
