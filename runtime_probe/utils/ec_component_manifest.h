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
        uint8_t reg;
        std::vector<uint8_t> write_data;
        std::optional<std::vector<uint8_t>> mask;
        std::optional<std::vector<uint8_t>> value;
        std::optional<uint8_t> override_addr;
        static std::optional<Expect> Create(const base::Value::Dict&);
        int bytes;
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

// A class that reads and parses an EC component manifest file to an
// EcComponentManifest instance.
class EcComponentManifestReader {
 public:
  explicit EcComponentManifestReader(std::string_view ec_version);
  EcComponentManifestReader(const EcComponentManifestReader&) = delete;
  EcComponentManifestReader& operator=(const EcComponentManifestReader&) =
      delete;
  virtual ~EcComponentManifestReader() = default;

  // Reads and parses the component manifest from default path, returning a
  // EcComponentManifest.
  // If the content is not a valid manifest, returns std::nullopt.
  std::optional<EcComponentManifest> Read() const;

  // Reads and parses the component manifest from the given path, returning a
  // EcComponentManifest.
  // If the content is not a valid manifest, returns std::nullopt.
  std::optional<EcComponentManifest> ReadFromFilePath(
      const base::FilePath&) const;

 protected:
  std::string ec_version_;

 private:
  // Returns the default path to the component manifest file. This should be
  // `/usr/share/cme/<ec-project-name>/component_manifest.json` where
  // `ec-project-name` can be obtained by `cros_config /firmware image-name`.
  virtual base::FilePath EcComponentManifestDefaultPath() const;
};

}  // namespace runtime_probe

#endif  // RUNTIME_PROBE_UTILS_EC_COMPONENT_MANIFEST_H_
