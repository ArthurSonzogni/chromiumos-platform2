// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sys/prctl.h>

#include <utility>

#include <base/at_exit.h>
#include <base/functional/bind.h>
#include <base/logging.h>
#include <base/memory/ref_counted.h>
#include <base/task/single_thread_task_executor.h>
#include <brillo/asynchronous_signal_handler.h>
#include <brillo/message_loops/base_message_loop.h>
#include <brillo/process/process_reaper.h>
#include <brillo/syslog_logging.h>
#include <dbus/bus.h>

#include "login_manager/arc_manager.h"
#include "login_manager/login_metrics.h"
#include "login_manager/system_utils_impl.h"

int main(int argc, char** argv) {
  base::AtExitManager exit_manager;
  brillo::InitLog(brillo::kLogToSyslog | brillo::kLogHeader);

  // Allow waiting for all descendants, not just immediate children.
  if (::prctl(PR_SET_CHILD_SUBREAPER, 1)) {
    PLOG(ERROR) << "Couldn't set child subreaper";
  }

  login_manager::SystemUtilsImpl system_utils;
  login_manager::LoginMetrics metrics(&system_utils);

  base::SingleThreadTaskExecutor task_executor(base::MessagePumpType::IO);
  brillo::BaseMessageLoop brillo_loop(task_executor.task_runner());
  brillo_loop.SetAsCurrent();

  brillo::AsynchronousSignalHandler signal_handler;
  signal_handler.Init();
  brillo::ProcessReaper process_reaper;
  process_reaper.Register(&signal_handler);

  dbus::Bus::Options options;
  options.bus_type = dbus::Bus::SYSTEM;
  scoped_refptr<dbus::Bus> bus = new dbus::Bus(std::move(options));
  CHECK(bus->Connect());
  CHECK(bus->SetUpAsyncOperations());

  login_manager::ArcManager arc_manager(system_utils, metrics, process_reaper,
                                        bus);
  arc_manager.Initialize();
  arc_manager.StartDBusService();

  signal_handler.RegisterHandler(
      SIGTERM, base::BindRepeating(
                   [](brillo::BaseMessageLoop* brillo_loop,
                      const struct signalfd_siginfo& siginfo) {
                     brillo_loop->BreakLoop();
                     return true;  // Unregister on returning.
                   },
                   base::Unretained(&brillo_loop)));
  brillo_loop.Run();

  arc_manager.Finalize();
  return 0;
}
