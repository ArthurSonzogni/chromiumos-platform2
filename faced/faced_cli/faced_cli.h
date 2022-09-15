// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FACED_FACED_CLI_FACED_CLI_H_
#define FACED_FACED_CLI_FACED_CLI_H_

#include <absl/status/statusor.h>

#include <string>
#include <vector>

namespace faced {

// Command for the tool to run.
enum class Command {
  kConnectToFaced,  // "connect"
};

// Parsed command line arguments.
struct CommandLineArgs {
  // Command specified by the user.
  Command command;

  inline friend bool operator==(const CommandLineArgs& a,
                                const CommandLineArgs& b) {
    return a.command == b.command;
  }
};

// Parse the given command line, producing a `CommandLine` on success.
absl::StatusOr<CommandLineArgs> ParseCommandLine(int argc,
                                                 const char* const* argv);

// Entry point to the `faced_cli` application.
int Main(int argc, char* argv[]);

}  // namespace faced

#endif  // FACED_FACED_CLI_FACED_CLI_H_
