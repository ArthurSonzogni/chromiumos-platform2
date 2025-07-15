// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RUNTIME_PROBE_UTILS_ISH_COMPONENT_MANIFEST_H_
#define RUNTIME_PROBE_UTILS_ISH_COMPONENT_MANIFEST_H_

#include <base/values.h>

#include "runtime_probe/utils/ec_component_manifest.h"

namespace base {
class FilePath;
}  // namespace base

namespace runtime_probe {

constexpr char kIshCmePath[] = "usr/share/cme/ish/";

// A class that reads and parses an ISH component manifest file to an
// EcComponentManifest instance.
class IshComponentManifestReader : public EcComponentManifestReader {
 public:
  using EcComponentManifestReader::EcComponentManifestReader;

 private:
  // Returns the default path to the component manifest file. This should be
  // `/usr/share/cme/ish/<ish-project-name>/component_manifest.json` where
  // `ish-project-name` can be obtained by parsing the output of
  // `ectool --name=cros_ish version`.
  base::FilePath EcComponentManifestDefaultPath() const override;
};

}  // namespace runtime_probe

#endif  // RUNTIME_PROBE_UTILS_ISH_COMPONENT_MANIFEST_H_
