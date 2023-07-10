// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_health_tool/diag/repliers/led_lit_up_routine_replier.h"

#include <iostream>
#include <optional>
#include <string>
#include <utility>

#include <base/check.h>

namespace diagnostics {

void LedLitUpRoutineReplier::GetColorMatched(GetColorMatchedCallback callback) {
  // Print a newline so we don't overwrite the progress percent.
  std::cout << '\n';

  std::optional<bool> answer;
  do {
    std::cout << "Is the LED lit up in the specified color? "
                 "Input y/n then press ENTER to continue."
              << std::endl;
    std::string input;
    std::getline(std::cin, input);

    if (!input.empty() && input[0] == 'y') {
      answer = true;
    } else if (!input.empty() && input[0] == 'n') {
      answer = false;
    }
  } while (!answer.has_value());

  CHECK(answer.has_value());
  std::move(callback).Run(answer.value());
}

}  // namespace diagnostics
