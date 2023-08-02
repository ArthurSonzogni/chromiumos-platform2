// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>
#include <optional>

#include <base/at_exit.h>
#include <base/command_line.h>
#include <base/json/json_reader.h>
#include <base/logging.h>
#include <base/message_loop/message_pump_type.h>
#include <base/run_loop.h>
#include <base/task/single_thread_task_executor.h>
#include <base/task/single_thread_task_runner.h>
#include <brillo/flag_helper.h>
#include <brillo/syslog_logging.h>
#include <mojo/core/embedder/embedder.h>
#include <mojo/core/embedder/scoped_ipc_support.h>

#include "runtime_probe/probe_config.h"
#include "runtime_probe/system/context_factory_impl.h"

int main(int argc, char* argv[]) {
  brillo::SetLogFlags(brillo::kLogToStderr);

  DEFINE_int32(log_level, 0,
               "Logging level - 0: LOG(INFO), 1: LOG(WARNING), 2: LOG(ERROR), "
               "-1: VLOG(1), -2: VLOG(2), ...");
  brillo::FlagHelper::Init(argc, argv, "ChromeOS factory runtime probe tool");

  logging::SetMinLogLevel(FLAGS_log_level);

  // Required by dbus in libchrome.
  base::AtExitManager at_exit_manager;
  runtime_probe::ContextFactoryImpl context;

  // Required by mojo
  base::SingleThreadTaskExecutor task_executor{base::MessagePumpType::IO};
  mojo::core::Init();
  mojo::core::ScopedIPCSupport ipc_support(
      base::SingleThreadTaskRunner::GetCurrentDefault(),
      mojo::core::ScopedIPCSupport::ShutdownPolicy::CLEAN);

  const auto* command_line = base::CommandLine::ForCurrentProcess();
  const auto args = command_line->GetArgs();

  for (size_t i = 0; i < args.size(); ++i) {
    DVLOG(1) << "Got arguments, index " << i << " = " << args[i];
  }

  if (args.size() != 1) {
    LOG(ERROR) << "factory_runtime_probe only consumes a single probe config.";
    return EXIT_FAILURE;
  }

  std::optional<base::Value> value = base::JSONReader::Read(args[0]);
  if (!value) {
    LOG(ERROR) << "Failed to parse probe config as json.";
    return EXIT_FAILURE;
  }

  auto probe_config = runtime_probe::ProbeConfig::FromValue(value.value());
  if (!probe_config) {
    LOG(ERROR) << "Failed to parse probe config.";
    return EXIT_FAILURE;
  }

  base::RunLoop run_loop;
  probe_config->Eval(base::BindOnce(
      [](base::OnceClosure quit_closure, base::Value::Dict probe_result) {
        std::cout << probe_result;
        std::move(quit_closure).Run();
      },
      run_loop.QuitClosure()));
  run_loop.Run();

  return EXIT_SUCCESS;
}
