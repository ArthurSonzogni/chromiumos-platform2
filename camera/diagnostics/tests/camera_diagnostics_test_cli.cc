// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdlib>
#include <memory>

#include <base/command_line.h>
#include <base/functional/bind.h>
#include <base/notreached.h>
#include <base/task/single_thread_task_runner.h>
#include <base/time/time.h>
#include <brillo/daemons/daemon.h>
#include <brillo/flag_helper.h>
#include <brillo/syslog_logging.h>
#include <chromeos/mojo/service_constants.h>
#include <mojo/core/embedder/embedder.h>
#include <mojo/core/embedder/scoped_ipc_support.h>
#include <mojo/public/cpp/bindings/remote.h>
#include <mojo_service_manager/lib/connect.h>

#include "camera/mojo/camera_diagnostics.mojom.h"
#include "cros-camera/common.h"
#include "diagnostics/camera_diagnostics_helpers.h"

namespace {
static void SetLogItems() {
  constexpr bool kOptionPID = true;
  constexpr bool kOptionTID = true;
  constexpr bool kOptionTimestamp = true;
  constexpr bool kOptionTickcount = true;
  logging::SetLogItems(kOptionPID, kOptionTID, kOptionTimestamp,
                       kOptionTickcount);
}

void OnDiagnosticsResult(
    cros::camera_diag::mojom::FrameAnalysisResultPtr result) {
  LOGF(INFO) << "Received the diagnostics result";
  switch (result->which()) {
    case cros::camera_diag::mojom::FrameAnalysisResult::Tag::kError:
      LOGF(INFO) << "Diagnostics Error: " << result->get_error();
      break;
    case cros::camera_diag::mojom::FrameAnalysisResult::Tag::kRes:
      LOGF(INFO) << "Diagnostics Result: "
                 << result->get_res()->suggested_issue;
      LOGF(INFO) << "Full result: "
                 << cros::DiagnosticsResultToJsonString(result->get_res());
      break;
    default:
      NOTREACHED_IN_MIGRATION();
  }
  exit(0);
}

void RunFrameAnalysis(
    const mojo::Remote<cros::camera_diag::mojom::CameraDiagnostics>& remote,
    int32_t duration_ms) {
  LOGF(INFO) << "Start RunFrameAnalysis";
  auto config = cros::camera_diag::mojom::FrameAnalysisConfig::New();
  config->client_type = cros::camera_diag::mojom::ClientType::kTest;
  config->duration_ms = duration_ms;
  remote->RunFrameAnalysis(std::move(config),
                           base::BindOnce(&OnDiagnosticsResult));
}
}  // namespace

int main(int argc, char* argv[]) {
  // Init CommandLine for InitLogging.
  base::CommandLine::Init(argc, argv);

  DEFINE_int32(duration, 5000,
               "Duration of the diagnosis in milliseconds, range [5000,60000]");

  brillo::InitLog(brillo::kLogToSyslog | brillo::kLogToStderrIfTty);
  // Override the log items set by brillo::InitLog.
  SetLogItems();
  brillo::FlagHelper::Init(argc, argv, "Camera diagnostics test cli");

  if (FLAGS_duration <
          cros::camera_diag::mojom::FrameAnalysisConfig::kMinDurationMs ||
      FLAGS_duration >
          cros::camera_diag::mojom::FrameAnalysisConfig::kMaxDurationMs) {
    LOGF(ERROR) << "Duration is out of range";
    return EXIT_FAILURE;
  }

  // Create the daemon instance first to properly set up MessageLoop and
  // AtExitManager.
  brillo::Daemon daemon;

  LOGF(INFO) << "Initialize mojo IPC";
  mojo::core::Init();
  auto ipc_support = std::make_unique<mojo::core::ScopedIPCSupport>(
      base::SingleThreadTaskRunner::GetCurrentDefault(),
      mojo::core::ScopedIPCSupport::ShutdownPolicy::
          CLEAN /* blocking shutdown */);

  LOGF(INFO) << "Connect to Mojo Service manager";
  mojo::Remote<chromeos::mojo_service_manager::mojom::ServiceManager>
      service_manager{
          chromeos::mojo_service_manager::ConnectToMojoServiceManager()};

  mojo::Remote<cros::camera_diag::mojom::CameraDiagnostics> diag_remote;
  LOGF(INFO) << "Request CameraDiagnostics";
  const base::TimeDelta kDiagnosticsRemoteRequestTimeout =
      base::Milliseconds(2 * 1000);
  service_manager->Request(chromeos::mojo_services::kCrosCameraDiagnostics,
                           kDiagnosticsRemoteRequestTimeout,
                           diag_remote.BindNewPipeAndPassReceiver().PassPipe());

  diag_remote.set_disconnect_handler(base::BindOnce([]() -> void {
    LOGF(ERROR) << "Disconnected from "
                << chromeos::mojo_services::kCrosCameraDiagnostics
                << ", aborting!";
    exit(0);
  }));

  RunFrameAnalysis(diag_remote, FLAGS_duration);

  LOGF(INFO) << "Run the camera diagnostics test daemon";
  daemon.Run();
  LOGF(INFO) << "Finished camera diagnostics test daemon";

  return 0;
}
