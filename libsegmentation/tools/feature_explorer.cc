// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// explorer allows to check the library is working and installed properly.

#include <iostream>
#include <string>

#include <brillo/flag_helper.h>
#include <libsegmentation/feature_management.h>

namespace segmentation {

void DumpFeatureLevel() {
  FeatureManagement feature_management;
  std::cout << feature_management.GetFeatureLevel() << std::endl;
}

void DumpIsFeatureEnabled(std::string feature) {
  FeatureManagement feature_management;
  std::cout << feature_management.IsFeatureEnabled(feature) << std::endl;
}

}  // namespace segmentation

int main(int argc, char* argv[]) {
  DEFINE_bool(feature_level, 0, "return the feature level for the device");
  DEFINE_bool(feature_dump, 0, "list all supported features");
  DEFINE_string(feature_name, "", "return true when the feature is supported");
  brillo::FlagHelper::Init(argc, argv, "Query the segmentation library");

  if (FLAGS_feature_level)
    segmentation::DumpFeatureLevel();
  else if (FLAGS_feature_name != "")
    segmentation::DumpIsFeatureEnabled(FLAGS_feature_name);

  return 0;
}
