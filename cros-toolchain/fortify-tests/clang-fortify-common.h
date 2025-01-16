// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CROS_TOOLCHAIN_FORTIFY_TESTS_CLANG_FORTIFY_COMMON_H_
#define CROS_TOOLCHAIN_FORTIFY_TESTS_CLANG_FORTIFY_COMMON_H_

#include <vector>

struct Failure {
  int line;
  const char* message;
  bool expected_death;
};

// Tests FORTIFY with -D_FORTIFY_SOURCE=1
std::vector<Failure> test_fortify_1();
// Tests FORTIFY with -D_FORTIFY_SOURCE=2
std::vector<Failure> test_fortify_2();

#endif  // CROS_TOOLCHAIN_FORTIFY_TESTS_CLANG_FORTIFY_COMMON_H_
