// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "runtime_probe/utils/ish_component_manifest.h"

#include <pcrecpp.h>

#include <optional>
#include <string>

#include <base/logging.h>

namespace runtime_probe {

std::optional<std::string> IshComponentManifestReader::GetCmeProjectName()
    const {
  pcrecpp::RE ec_version_re(R"(([^-]+(?:-ish)?)-.*)");
  std::string ish_project_name;
  if (ec_version_re.FullMatch(ec_version_, &ish_project_name)) {
    return ish_project_name;
  }

  LOG(ERROR) << "Failed to get ISH project name from EC version \""
             << ec_version_ << "\".";
  return std::nullopt;
}

}  // namespace runtime_probe
