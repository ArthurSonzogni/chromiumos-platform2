// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/command_line.h>
#include <brillo/flag_helper.h>
#include <gtest/gtest.h>
#include <ostream>
#include <string>

namespace sane_backend_tests {
// Scanner + Backend under test
const std::string* scanner_under_test;
}  // namespace sane_backend_tests

int main(int argc, char** argv) {
  DEFINE_string(scanner, "", "Name of the backend-under-test opened scanner.");

  brillo::FlagHelper::Init(
      argc, argv,
      "sane_backend_wwcb_test, command-line interface to, "
      "GoogleTest tests for WWCB validation of a SANE backend. Any arguments "
      "passed after \"--\" are passed directly to GoogleTest.");

  sane_backend_tests::scanner_under_test = &FLAGS_scanner;

  if (*sane_backend_tests::scanner_under_test == "") {
    std::cerr << "Requires --scanner=<scanner> flag. Please use a scanner name "
                 "generated from \"lorgnette_cli discover\"."
              << std::endl;
    return 1;
  }

  // We want to pass gtest arguments directly to gtest.
  // We adjust by 1 argument to include the command, otherwise InitGoogleTest
  // fails.
  int gtest_argc = base::CommandLine::ForCurrentProcess()->GetArgs().size() + 1;
  char** gtest_argv = argv + (argc - gtest_argc);
  // InitGoogleTest needs the commandline arg.
  gtest_argv[0] = argv[0];
  testing::InitGoogleTest(&gtest_argc, gtest_argv);

  return RUN_ALL_TESTS();
}
