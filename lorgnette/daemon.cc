// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lorgnette/daemon.h"

#include <sysexits.h>

#include <string>
#include <utility>

#include <base/bind.h>
#include <base/logging.h>
#include <base/run_loop.h>
#include <base/threading/thread_task_runner_handle.h>
#include <base/time/time.h>
#include <chromeos/dbus/service_constants.h>

#include "lorgnette/sane_client_impl.h"

using std::string;

namespace lorgnette {

// static
const char Daemon::kScanGroupName[] = "scanner";
const char Daemon::kScanUserName[] = "saned";

Daemon::Daemon(base::OnceClosure startup_callback)
    : DBusServiceDaemon(kManagerServiceName, "/ObjectManager"),
      startup_callback_(std::move(startup_callback)) {}

int Daemon::OnInit() {
  int return_code = brillo::DBusServiceDaemon::OnInit();
  if (return_code != EX_OK) {
    return return_code;
  }

  PostponeShutdown(kNormalShutdownTimeout);

  // Signal that we've acquired all resources.
  std::move(startup_callback_).Run();
  return EX_OK;
}

void Daemon::RegisterDBusObjectsAsync(
    brillo::dbus_utils::AsyncEventSequencer* sequencer) {
  manager_.reset(new Manager(base::BindRepeating(&Daemon::PostponeShutdown,
                                                 weak_factory_.GetWeakPtr()),
                             SaneClientImpl::Create()));
  manager_->RegisterAsync(object_manager_.get(), sequencer);
}

void Daemon::OnShutdown(int* return_code) {
  manager_.reset();
  brillo::DBusServiceDaemon::OnShutdown(return_code);
}

void Daemon::PostponeShutdown(base::TimeDelta delay) {
  shutdown_callback_.Reset(
      base::BindOnce(&brillo::Daemon::Quit, weak_factory_.GetWeakPtr()));
  base::ThreadTaskRunnerHandle::Get()->PostDelayedTask(
      FROM_HERE, shutdown_callback_.callback(), delay);
}

}  // namespace lorgnette
