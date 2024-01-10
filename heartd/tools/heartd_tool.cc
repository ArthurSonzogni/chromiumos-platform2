// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/at_exit.h>
#include <base/check.h>
#include <base/functional/bind.h>
#include <base/no_destructor.h>
#include <base/notreached.h>
#include <base/run_loop.h>
#include <base/task/single_thread_task_executor.h>
#include <base/task/single_thread_task_runner.h>
#include <brillo/flag_helper.h>
#include <brillo/syslog_logging.h>
#include <chromeos/mojo/service_constants.h>
#include <mojo/core/embedder/embedder.h>
#include <mojo/core/embedder/scoped_ipc_support.h>
#include <mojo/public/cpp/bindings/remote.h>
#include <mojo_service_manager/lib/connect.h>

#include "heartd/mojom/heartd.mojom.h"

namespace {

namespace mojom = ::ash::heartd::mojom;

chromeos::mojo_service_manager::mojom::ServiceManager*
GetServiceManagerProxy() {
  static const base::NoDestructor<
      mojo::Remote<chromeos::mojo_service_manager::mojom::ServiceManager>>
      remote(chromeos::mojo_service_manager::ConnectToMojoServiceManager());

  CHECK(remote->is_bound()) << "Failed to connect to mojo service manager.";
  return remote->get();
}

// Requests a service and sets the disconnect handler to raise fatal error on
// disconnect.
template <typename T>
void RequestMojoServiceWithDisconnectHandler(const std::string& service_name,
                                             mojo::Remote<T>& remote) {
  GetServiceManagerProxy()->Request(
      service_name, /*timeout=*/std::nullopt,
      remote.BindNewPipeAndPassReceiver().PassPipe());
  remote.set_disconnect_with_reason_handler(base::BindOnce(
      [](const std::string& service_name, uint32_t error,
         const std::string& reason) {
        LOG(FATAL) << "Service " << service_name
                   << " disconnected, error: " << error
                   << ", reason: " << reason;
      },
      service_name));
}

mojom::ActionType GetActionEnum(std::string& action) {
  if (action == "kNoOperation") {
    return mojom::ActionType::kNoOperation;
  } else if (action == "kNormalReboot") {
    return mojom::ActionType::kNormalReboot;
  } else if (action == "kForceReboot") {
    return mojom::ActionType::kForceReboot;
  }

  LOG(ERROR) << "Unknow action: " << action;
  NOTREACHED_NORETURN();
}

}  // namespace

int main(int argc, char* argv[]) {
  brillo::InitLog(brillo::kLogToStderr);
  DEFINE_bool(enable_normal_reboot, false, "Whether to enable normal reboot.");
  DEFINE_bool(enable_force_reboot, false, "Whether to enable force reboot.");
  DEFINE_bool(stop_monitor, false,
              "Stop monitor, this will be called before simulating client "
              "missing.");
  DEFINE_bool(simulate_client_missing, false,
              "When this is set to true, this tool exits immediately after "
              "registration, so it won't send heartbeat anymore.");
  DEFINE_uint32(verification_window_seconds, 70,
                "The verification window. Minimum is 70 seconds.");
  // For testing, supporting two actions should be enough.
  DEFINE_string(action1, "kNoOperation",
                "The first action to take: "
                "[kNoOperation, kNormalReboot, kForceReboot]");
  DEFINE_string(action2, "kNoOperation",
                "The second action to take: "
                "[kNoOperation, kNormalReboot, kForceReboot]");
  brillo::FlagHelper::Init(argc, argv, "Heartd test tool");

  // Initialize the mojo environment.
  base::AtExitManager at_exit_manager;
  base::SingleThreadTaskExecutor task_executor(base::MessagePumpType::IO);
  mojo::core::Init();
  mojo::core::ScopedIPCSupport ipc_support(
      base::SingleThreadTaskRunner::
          GetCurrentDefault() /* io_thread_task_runner */,
      mojo::core::ScopedIPCSupport::ShutdownPolicy::
          CLEAN /* blocking shutdown */);

  mojo::Remote<mojom::HeartdControl> control_remote;
  RequestMojoServiceWithDisconnectHandler(
      chromeos::mojo_services::kHeartdControl, control_remote);
  if (FLAGS_enable_normal_reboot) {
    control_remote->EnableNormalRebootAction();
  }
  if (FLAGS_enable_force_reboot) {
    control_remote->EnableForceRebootAction();
  }

  auto argument = mojom::HeartbeatServiceArgument::New();
  argument->verification_window_seconds = FLAGS_verification_window_seconds;
  argument->actions.push_back(mojom::Action::New(
      /*failure_count = */ 1, GetActionEnum(FLAGS_action1)));
  argument->actions.push_back(mojom::Action::New(
      /*failure_count = */ 2, GetActionEnum(FLAGS_action2)));

  base::RunLoop register_run_loop;
  mojo::Remote<mojom::HeartbeatService> hb_remote;
  RequestMojoServiceWithDisconnectHandler(
      chromeos::mojo_services::kHeartdHeartbeatService, hb_remote);
  mojo::Remote<mojom::Pacemaker> pacemaker;
  hb_remote->Register(mojom::ServiceName::kKiosk, std::move(argument),
                      pacemaker.BindNewPipeAndPassReceiver(),
                      base::BindOnce(
                          [](base::OnceClosure quit_closure, bool success) {
                            if (success) {
                              LOG(INFO) << "Registration success.";
                            } else {
                              LOG(FATAL) << "Registration fail";
                            }
                            std::move(quit_closure).Run();
                          },
                          register_run_loop.QuitClosure()));

  register_run_loop.Run();

  if (FLAGS_stop_monitor) {
    pacemaker->StopMonitor(base::DoNothing());
    return EXIT_SUCCESS;
  }

  // Exit without sending heartbeat. This makes heartd to perform the actions.
  if (FLAGS_simulate_client_missing) {
    return EXIT_SUCCESS;
  }

  // Registration is complete, we can start sending heartbeat now.
  LOG(INFO) << "Start sending heartbeat for every minute. Kill this process to "
            << "simulate client error at any time.";
  while (true) {
    pacemaker->SendHeartbeat(base::DoNothing());
    sleep(60);
  }
}
