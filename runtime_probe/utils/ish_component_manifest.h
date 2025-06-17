// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RUNTIME_PROBE_UTILS_ISH_COMPONENT_MANIFEST_H_
#define RUNTIME_PROBE_UTILS_ISH_COMPONENT_MANIFEST_H_

#include <optional>
#include <string>

#include <base/values.h>

#include "runtime_probe/utils/ec_component_manifest.h"

namespace base {
class FilePath;
}  // namespace base

namespace runtime_probe {

// A class that reads and parses an ISH component manifest file to an
// EcComponentManifest instance.
class IshComponentManifestReader : public EcComponentManifestReader {
 public:
  using EcComponentManifestReader::EcComponentManifestReader;

 private:
  // Gets the ISH firmware name with `ectool --name=cros_ish version`.
  std::optional<std::string> GetCmeProjectName() const override;
};

}  // namespace runtime_probe

#endif  // RUNTIME_PROBE_UTILS_ISH_COMPONENT_MANIFEST_H_
