// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flex_hwis/flex_hardware_cache.h"
#include "flex_hwis/flex_hwis.h"
#include "flex_hwis/flex_hwis_mojo.h"
#include "flex_hwis/flex_hwis_server_info.h"
#include "flex_hwis/http_sender.h"

#include <memory>

#include <chromeos/constants/flex_hwis.h>
#include <base/at_exit.h>
#include <base/logging.h>
#include <base/task/single_thread_task_executor.h>
#include <base/task/single_thread_task_runner.h>
#include <brillo/flag_helper.h>
#include <brillo/syslog_logging.h>
#include <metrics/metrics_library.h>
#include <mojo/core/embedder/embedder.h>
#include <mojo/core/embedder/scoped_ipc_support.h>

namespace {

// Sends hardware data to the server, if allowed.
// Returns false only if a problem occurred.
bool Send(hwis_proto::Device& hardware_info) {
  auto server_info = flex_hwis::ServerInfo();
  auto sender = flex_hwis::HttpSender(server_info);
  auto provider = policy::PolicyProvider();
  flex_hwis::FlexHwisSender flex_hwis_sender(base::FilePath("/"), provider,
                                             sender);
  MetricsLibrary metrics_library;
  auto flex_hwis_res =
      flex_hwis_sender.MaybeSend(hardware_info, metrics_library);

  switch (flex_hwis_res) {
    case flex_hwis::Result::Sent:
      LOG(INFO) << "flex_hwis_tool sent successfully";
      break;
    case flex_hwis::Result::HasRunRecently:
      LOG(INFO) << "flex_hwis_tool cannot send again within 24 hours";
      break;
    case flex_hwis::Result::NotAuthorized:
      LOG(INFO) << "flex_hwis_tool wasn't authorized to send data";
      break;
    case flex_hwis::Result::Error:
      LOG(WARNING) << "flex_hwis_tool encountered an error";
      break;
    default:
      LOG(WARNING) << "flex_hwis_tool has unexpected return value";
      return false;
  }
  return true;
}

// Caches hardware info on disk.
// Returns false if any data couldn't be written.
bool Cache(hwis_proto::Device& hardware_info) {
  LOG(INFO) << "writing cache";
  return flex_hwis::WriteCacheToDisk(hardware_info, base::FilePath("/"));
}

}  // namespace

int main(int argc, char** argv) {
  DEFINE_bool(debug, false, "Whether to dump the data for debugging purposes");
  DEFINE_bool(cache, false, "Whether to cache data for other tools to read");
  DEFINE_bool(send, false, "Whether to send the data to the server");
  brillo::FlagHelper::Init(argc, argv,
                           "ChromeOS Flex Hardware Information Service");
  brillo::InitLog(FLAGS_debug ? brillo::kLogToStderr : brillo::kLogToSyslog);

  if (!FLAGS_debug && !FLAGS_cache && !FLAGS_send) {
    LOG(WARNING) << "No operation requested. Exiting early.";
    return 0;
  }

  // Initialize the mojo environment.
  base::AtExitManager at_exit_manager;
  base::SingleThreadTaskExecutor task_executor(base::MessagePumpType::IO);
  mojo::core::Init();
  mojo::core::ScopedIPCSupport ipc_support(
      base::SingleThreadTaskRunner::GetCurrentDefault(),
      // We don't pass message pipes to other processes, so use FAST shutdown.
      // See scoped_ipc_support.h
      mojo::core::ScopedIPCSupport::ShutdownPolicy::FAST);

  // Fill our proto with hardware info.
  hwis_proto::Device hardware_info;
  flex_hwis::FlexHwisMojo mojo;
  mojo.SetHwisInfo(&hardware_info);

  if (FLAGS_debug) {
    LOG(INFO) << hardware_info.DebugString();
  }

  bool success = true;
  if (FLAGS_send && !Send(hardware_info)) {
    success = false;
  }

  if (FLAGS_cache && !Cache(hardware_info)) {
    success = false;
  }

  return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
