// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>
#include <cmath>
#include <cstdint>

#include <base/time/time.h>
#include <brillo/flag_helper.h>

#include "diagnostics/routines/prime_search/prime_number_search.h"

namespace {

// Largest number that routine will calculate prime numbers up to.
constexpr uint64_t kMaxNumber = 1000000;

}  // namespace

// 'prime_search' command-line tool:
//  Calculates prime number between 2 to max_num and verifies the calculation
//  repeatedly within a duration.
int main(int argc, char** argv) {
  DEFINE_uint64(time, 10, "duration in seconds to run routine for.");
  DEFINE_uint64(max_num, kMaxNumber,
                "search for prime number less or equal to max_num. "
                "Max and default is 1000000");
  brillo::FlagHelper::Init(argc, argv, "prime_search - diagnostic routine.");

  base::TimeTicks end_time =
      base::TimeTicks::Now() + base::TimeDelta::FromSeconds(FLAGS_time);

  uint64_t max_num = kMaxNumber;
  if (FLAGS_max_num <= kMaxNumber && FLAGS_max_num >= 2)
    max_num = FLAGS_max_num;

  auto prime_number_search =
      std::make_unique<diagnostics::PrimeNumberSearch>(max_num);
  bool result = false;

  while (base::TimeTicks::Now() < end_time) {
    result = prime_number_search->Run();
    if (result == false)
      break;
  }

  return result == true ? EXIT_SUCCESS : EXIT_FAILURE;
}
