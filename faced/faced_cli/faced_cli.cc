// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "faced/faced_cli/faced_cli.h"

#include <string>
#include <vector>

#include <absl/status/status.h>
#include <absl/status/statusor.h>
#include <absl/strings/str_format.h>
#include <base/check.h>
#include <base/command_line.h>
#include <base/strings/string_piece.h>
#include <base/task/single_thread_task_executor.h>
#include <base/threading/thread.h>
#include <brillo/flag_helper.h>
#include <mojo/core/embedder/embedder.h>
#include <mojo/core/embedder/scoped_ipc_support.h>

#include "faced/faced_cli/faced_client.h"

namespace faced {

namespace {

// CLI documentation.
constexpr base::StringPiece kUsage = R"(Usage: faced_cli <command> [options]

Commands:
  connect             Set up a Mojo connection to Faced by bootstrapping over
                      Dbus and then disconnect the session.

  enroll              Enroll a user
    --user=<string>     User to enroll (eg. someone).

Full details of options can be shown using "--help".
)";

// Parse a command string into the enum type `Command`.
std::optional<Command> ParseCommand(base::StringPiece command) {
  if (command == "connect") {
    return Command::kConnectToFaced;
  }
  if (command == "enroll") {
    return Command::kEnroll;
  }
  return std::nullopt;
}

absl::Status RunCommand(const CommandLineArgs& command) {
  switch (command.command) {
    case Command::kConnectToFaced:
      return ConnectAndDisconnectFromFaced();
    case Command::kEnroll:
      return Enroll(command.user);
  }
}

}  // namespace

// Parse the given command line, producing a `CommandLineArgs` on success.
absl::StatusOr<CommandLineArgs> ParseCommandLine(int argc,
                                                 const char* const* argv) {
  CHECK(argc > 0)
      << "Argv must contain at least one element, the program name.";

  DEFINE_string(user, "", "User to enroll (eg. someone).");

  if (!brillo::FlagHelper::Init(argc, argv, std::string(kUsage),
                                brillo::FlagHelper::InitFuncType::kReturn)) {
    return absl::InvalidArgumentError("Invalid option.");
  }

  // Parse the sub-command.
  std::vector<std::string> commands =
      base::CommandLine::ForCurrentProcess()->GetArgs();
  if (commands.size() != 1) {
    return absl::InvalidArgumentError("Expected exactly one command.");
  }
  std::optional<Command> command = ParseCommand(commands[0]);
  if (!command.has_value()) {
    return absl::InvalidArgumentError(
        absl::StrFormat("Unknown command '%s'.", commands[0]));
  }

  switch (command.value()) {
    case Command::kConnectToFaced:
      if (!FLAGS_user.empty()) {
        return absl::InvalidArgumentError(
            absl::StrFormat("--user argument '%s' was provided for 'connect' "
                            "command which does not use this argument.",
                            FLAGS_user));
      }
      break;
    case Command::kEnroll:
      if (FLAGS_user.empty()) {
        return absl::InvalidArgumentError(
            "No --user argument was provided for 'enroll' command.");
      }
      break;
  }

  return CommandLineArgs{
      .command = *command,
      .user = FLAGS_user,
  };
}

int Main(int argc, char* argv[]) {
  // Setup task context
  base::SingleThreadTaskExecutor task_executor(base::MessagePumpType::IO);

  // Basic Mojo initialization for a new process.
  mojo::core::Init();
  base::Thread ipc_thread("FacedCliIpc");
  ipc_thread.StartWithOptions(
      base::Thread::Options(base::MessagePumpType::IO, 0));
  mojo::core::ScopedIPCSupport ipc_support(
      ipc_thread.task_runner(),
      mojo::core::ScopedIPCSupport::ShutdownPolicy::CLEAN);

  // Parse command line.
  absl::StatusOr<CommandLineArgs> result = ParseCommandLine(argc, argv);
  if (!result.ok()) {
    std::cerr << kUsage << "\n"
              << "Error: " << result.status().message() << "\n";
    return 1;
  }

  // Run the appropriate command.
  absl::Status command_result = RunCommand(*result);
  if (!command_result.ok()) {
    std::cerr << "Error: " << command_result.message() << "\n";
    return 1;
  }

  return 0;
}

}  // namespace faced
