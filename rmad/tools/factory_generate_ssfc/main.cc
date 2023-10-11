// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sys/types.h>
#include <unistd.h>
#include <iomanip>

#include <base/logging.h>
#include <base/task/single_thread_task_executor.h>
#include <brillo/flag_helper.h>
#include <brillo/syslog_logging.h>

#include <rmad/ssfc/ssfc_prober.h>

int main(int argc, char* argv[]) {
  brillo::SetLogFlags(brillo::kLogToStderr);

  DEFINE_int32(log_level, 0,
               "Logging level - 0: LOG(INFO), 1: LOG(WARNING), 2: LOG(ERROR), "
               "-1: VLOG(1), -2: VLOG(2), ...");
  brillo::FlagHelper::Init(argc, argv, "ChromeOS generate SSFC tool");

  logging::SetMinLogLevel(FLAGS_log_level);

  base::SingleThreadTaskExecutor task_executor{base::MessagePumpType::IO};

  rmad::SsfcProberImpl ssfc_prober;

  if (ssfc_prober.IsSsfcRequired()) {
    if (uint32_t ssfc_value; ssfc_prober.ProbeSsfc(&ssfc_value)) {
      std::cout << "0x" << std::setfill('0') << std::setw(2) << std::hex
                << ssfc_value << std::endl;
      return EXIT_SUCCESS;
    }
    LOG(ERROR) << "Failed to probe SSFC";
    return EXIT_FAILURE;
  }

  std::cout << "SSFC is not required." << std::endl;
  return EXIT_SUCCESS;
}
