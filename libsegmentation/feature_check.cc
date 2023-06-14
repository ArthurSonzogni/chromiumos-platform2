// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// simple executable to encapsulation libsegmentation library to check from the
// command line if a feature is enabled. The commands are purposely limited as
// this executable is installed on all images.

#include <iostream>

#include <base/logging.h>
#include <brillo/flag_helper.h>
#include <libsegmentation/feature_management.h>

int main(int argc, char* argv[]) {
  DEFINE_string(feature_name, "", "return true when the feature is supported");
  DEFINE_bool(feature_level, false, "return the feature level for the device");
  DEFINE_bool(scope_level, false, "return the scope level for the device");
  brillo::FlagHelper::Init(argc, argv, "Query the segmentation library");

  const base::CommandLine* cl = base::CommandLine::ForCurrentProcess();
  if (cl->GetArgs().size() > 0) {
    LOG(ERROR) << "Unknown extra command line arguments; exiting";
    return EXIT_FAILURE;
  }

  segmentation::FeatureManagement feature_management;
  if (FLAGS_feature_level) {
    std::cout << feature_management.GetFeatureLevel() << std::endl;
  } else if (FLAGS_scope_level) {
    std::cout << feature_management.GetScopeLevel() << std::endl;
  } else if (!FLAGS_feature_name.empty()) {
    std::cout << feature_management.IsFeatureEnabled(FLAGS_feature_name)
              << std::endl;
  } else {
    LOG(ERROR) << "Please specify an option to control execution mode.";
    return EXIT_FAILURE;
  }

  return 0;
}
