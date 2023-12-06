// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flex_id/flex_id.h"
#include "flex_id/flex_state_key.h"

#include <iostream>
#include <string>

#include <base/files/file_path.h>
#include <base/logging.h>
#include <brillo/flag_helper.h>
#include <brillo/syslog_logging.h>

int main(int argc, char* argv[]) {
  brillo::InitLog(brillo::kLogToSyslog);
  DEFINE_string(type, "", "Select type from {id, state_key}");
  brillo::FlagHelper::Init(argc, argv, "ChromeOS Flex ID Tool");

  if (FLAGS_type == "id") {
    flex_id::FlexIdGenerator flex_id_generator(base::FilePath("/"));
    auto flex_id = flex_id_generator.GenerateAndSaveFlexId();
    if (!flex_id) {
      LOG(ERROR) << "Couldn't save flex_id. Exiting.";
      return EXIT_FAILURE;
    }
    std::cout << flex_id.value() << std::endl;
    LOG(INFO) << "flex_id_tool ID ran successfully. Exiting.";
  } else if (FLAGS_type == "state_key") {
    flex_id::FlexStateKeyGenerator flex_state_key_gen(base::FilePath("/"));
    auto flex_state_key = flex_state_key_gen.GenerateAndSaveFlexStateKey();
    if (!flex_state_key) {
      LOG(ERROR) << "Couldn't save flex_state_key. Exiting.";
      return EXIT_FAILURE;
    }
    std::cout << flex_state_key.value() << std::endl;
    LOG(INFO) << "flex_id_tool State Key ran successfully. Exiting.";
  } else {
    LOG(ERROR)
        << "flex_id_tool did nothing. No type argument specified. Exiting.";
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
