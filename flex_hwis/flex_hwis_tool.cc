// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flex_hwis/flex_hwis.h"

#include <memory>

#include <base/at_exit.h>
#include <base/logging.h>
#include <base/task/single_thread_task_executor.h>
#include <base/task/single_thread_task_runner.h>
#include <brillo/flag_helper.h>
#include <brillo/syslog_logging.h>
#include <metrics/metrics_library.h>
#include <mojo/core/embedder/embedder.h>
#include <mojo/core/embedder/scoped_ipc_support.h>

int main(int argc, char** argv) {
  DEFINE_bool(debug, false, "Whether to dump the data for debugging purposes");
  brillo::FlagHelper::Init(argc, argv,
                           "ChromeOS Flex Hardware Information Service");
  brillo::InitLog(FLAGS_debug ? brillo::kLogToStderr : brillo::kLogToSyslog);

  // Initialize the mojo environment.
  base::AtExitManager at_exit_manager;
  base::SingleThreadTaskExecutor task_executor(base::MessagePumpType::IO);
  mojo::core::Init();
  mojo::core::ScopedIPCSupport ipc_support(
      base::SingleThreadTaskRunner::
          GetCurrentDefault() /* io_thread_task_runner */,
      mojo::core::ScopedIPCSupport::ShutdownPolicy::
          CLEAN /* blocking shutdown */);

  MetricsLibrary metrics_library;
  auto provider = std::make_unique<policy::PolicyProvider>();
  flex_hwis::FlexHwisSender flex_hwis_sender(base::FilePath("/"), *provider);
  auto flex_hwis_res = flex_hwis_sender.CollectAndSend(
      metrics_library,
      FLAGS_debug ? flex_hwis::Debug::Print : flex_hwis::Debug::None);

  switch (flex_hwis_res) {
    case flex_hwis::Result::Sent:
      LOG(INFO) << "flex_hwis_tool ran successfully";
      break;
    case flex_hwis::Result::HasRunRecently:
      LOG(INFO) << "flex_hwis_tool cannot be run within 24 hour";
      break;
    case flex_hwis::Result::NotAuthorized:
      LOG(INFO) << "flex_hwis_tool wasn't authorized to send data";
      break;
    default:
      LOG(INFO) << "flex_hwis_tool has unexpected return value";
  }
  return 0;
}
