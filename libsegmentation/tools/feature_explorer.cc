// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// explorer allows to check the library is working and installed properly.

#include <iostream>
#include <string>

#include <base/logging.h>
#include <brillo/flag_helper.h>
#include <libsegmentation/feature_management.h>

namespace segmentation {

void DumpFeatureLevel() {
  FeatureManagement feature_management;
  std::cout << feature_management.GetFeatureLevel() << std::endl;
}

void DumpScopeLevel() {
  FeatureManagement feature_management;
  std::cout << feature_management.GetScopeLevel() << std::endl;
}

void DumpIsFeatureEnabled(std::string feature) {
  FeatureManagement feature_management;
  std::cout << feature_management.IsFeatureEnabled(feature) << std::endl;
}

void DumpFeatureList(const FeatureUsage usage) {
  FeatureManagement feature_management;
  const std::set<std::string> features = feature_management.ListFeatures(usage);
  for (auto feature : features) {
    std::cout << feature << std::endl;
  }
}

}  // namespace segmentation

int main(int argc, char* argv[]) {
  DEFINE_bool(feature_level, false, "return the feature level for the device");
  DEFINE_bool(scope_level, false, "return the scope level for the device");
  DEFINE_string(feature_list, "",
                "list all supported features for a given subsystem: chrome, "
                "chromeos, android");
  DEFINE_string(feature_name, "", "return true when the feature is supported");
  brillo::FlagHelper::Init(argc, argv, "Query the segmentation library");

  const base::CommandLine* cl = base::CommandLine::ForCurrentProcess();
  if (cl->GetArgs().size() > 0) {
    LOG(ERROR) << "Unknown extra command line arguments; exiting";
    return EXIT_FAILURE;
  }

  segmentation::FeatureManagement feature_management;
  if (FLAGS_feature_level) {
    segmentation::DumpFeatureLevel();
  } else if (FLAGS_scope_level) {
    segmentation::DumpScopeLevel();
  } else if (!FLAGS_feature_name.empty()) {
    segmentation::DumpIsFeatureEnabled(FLAGS_feature_name);
  } else if (!FLAGS_feature_list.empty()) {
    if (!FLAGS_feature_list.compare("chrome")) {
      segmentation::DumpFeatureList(segmentation::USAGE_CHROME);
    } else if (!FLAGS_feature_list.compare("chromeos")) {
      segmentation::DumpFeatureList(segmentation::USAGE_LOCAL);
    } else if (!FLAGS_feature_list.compare("android")) {
      segmentation::DumpFeatureList(segmentation::USAGE_ANDROID);
    } else {
      LOG(ERROR) << "Invalid subsystem";
      return EXIT_FAILURE;
    }
  } else {
    LOG(ERROR) << "Please specify an option to control execution mode.";
    return EXIT_FAILURE;
  }
  return 0;
}
