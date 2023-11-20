// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <stdlib.h>
#include <sysexits.h>

#include <base/command_line.h>
#include <base/logging.h>
#include <brillo/syslog_logging.h>

#include "libhwsec-foundation/tpm/tpm_clear.h"

namespace {
constexpr char kUsage[] = R"(
Usage: tpm_clear_request [value]
  Return the current value or set the |value|.
  The valid inputs of |value| are "0" and "1".
)";

int PrintUsage() {
  printf("%s", kUsage);
  return EX_USAGE;
}
}  // namespace

int main(int argc, char* argv[]) {
  base::CommandLine::Init(argc, argv);
  brillo::InitLog(brillo::kLogToSyslog | brillo::kLogToStderr);
  base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();
  if (command_line->HasSwitch("help") || command_line->HasSwitch("h")) {
    return PrintUsage();
  }

  // Get the current value.
  if (command_line->GetArgs().size() == 0) {
    std::optional<bool> value = hwsec_foundation::tpm::GetClearTpmRequest();
    if (!value.has_value()) {
      return -1;
    }
    printf("%d\n", static_cast<int>(*value));
    return static_cast<int>(*value);
  }

  // Set the current value.
  std::string command = command_line->GetArgs()[0];

  bool value = false;
  if (command == "0") {
    value = false;
  } else if (command == "1") {
    value = true;
  } else {
    return PrintUsage();
  }

  if (!hwsec_foundation::tpm::SetClearTpmRequest(value)) {
    return -1;
  }

  return EX_OK;
}
