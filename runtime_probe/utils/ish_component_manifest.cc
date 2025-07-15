// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "runtime_probe/utils/ish_component_manifest.h"

#include <pcrecpp.h>

#include <optional>
#include <string>

#include <base/logging.h>

#include "runtime_probe/system/context.h"
#include "runtime_probe/utils/ec_component_manifest.h"

namespace runtime_probe {

namespace {

std::optional<std::string> GetIshProjetName(const std::string& ec_version) {
  pcrecpp::RE ec_version_re(R"(([^-]+(?:-ish)?)-.*)");
  std::string ish_project_name;
  if (ec_version_re.FullMatch(ec_version, &ish_project_name)) {
    return ish_project_name;
  }

  LOG(ERROR) << "Failed to get ISH project name from EC version \""
             << ec_version << "\".";
  return std::nullopt;
}

}  // namespace

base::FilePath IshComponentManifestReader::EcComponentManifestDefaultPath()
    const {
  const auto ish_project_name = GetIshProjetName(ec_version_);
  if (!ish_project_name.has_value()) {
    return {};
  }
  return Context::Get()
      ->root_dir()
      .Append(kIshCmePath)
      .Append(*ish_project_name)
      .Append(kEcComponentManifestName);
}

}  // namespace runtime_probe
