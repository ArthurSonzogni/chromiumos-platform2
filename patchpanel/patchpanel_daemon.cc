// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/patchpanel_daemon.h"

#include <sys/prctl.h>

#include <utility>

#include <base/logging.h>
#include <chromeos/dbus/service_constants.h>
#include <metrics/metrics_library.h>
#include <net-base/process_manager.h>

#include "patchpanel/metrics.h"
#include "patchpanel/patchpanel_adaptor.h"
#include "patchpanel/rtnl_client.h"
#include "patchpanel/system.h"

namespace patchpanel {

PatchpanelDaemon::PatchpanelDaemon(const base::FilePath& cmd_path)
    : DBusServiceDaemon(kPatchPanelServiceName),
      cmd_path_(cmd_path),
      system_(std::make_unique<System>()),
      process_manager_(net_base::ProcessManager::GetInstance()),
      metrics_(std::make_unique<MetricsLibrary>()) {}

void PatchpanelDaemon::RegisterDBusObjectsAsync(
    brillo::dbus_utils::AsyncEventSequencer* sequencer) {
  prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);

  // Initialize |process_manager_| before creating subprocesses.
  process_manager_->Init();

  auto rtnl_client = RTNLClient::Create();
  if (!rtnl_client) {
    LOG(ERROR) << "Failed to create RTNLClient, abort registering the adaptor";
    return;
  }

  adaptor_ = std::make_unique<PatchpanelAdaptor>(
      cmd_path_, bus_, system_.get(), process_manager_, metrics_.get(),
      std::move(rtnl_client));
  adaptor_->RegisterAsync(
      sequencer->GetHandler("RegisterAsync() failed", true));
}

void PatchpanelDaemon::OnShutdown(int* exit_code) {
  LOG(INFO) << "Shutting down and cleaning up";

  adaptor_.reset();

  // Stop |process_manager_| after subprocesses are finished.
  process_manager_->Stop();

  if (bus_) {
    bus_->ShutdownAndBlock();
  }
  brillo::DBusDaemon::OnShutdown(exit_code);
}

}  // namespace patchpanel
