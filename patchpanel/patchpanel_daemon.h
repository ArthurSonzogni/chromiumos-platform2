// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHPANEL_PATCHPANEL_DAEMON_H_
#define PATCHPANEL_PATCHPANEL_DAEMON_H_

#include <memory>

#include <base/files/file_path.h>
#include <brillo/daemons/dbus_daemon.h>
#include <metrics/metrics_library.h>
#include <net-base/process_manager.h>

#include "patchpanel/patchpanel_adaptor.h"
#include "patchpanel/system.h"

namespace patchpanel {

// Main class that runs the main loop and responds to D-Bus RPC requests.
class PatchpanelDaemon final : public brillo::DBusServiceDaemon {
 public:
  explicit PatchpanelDaemon(const base::FilePath& cmd_path);
  PatchpanelDaemon(const PatchpanelDaemon&) = delete;
  PatchpanelDaemon& operator=(const PatchpanelDaemon&) = delete;

  ~PatchpanelDaemon() = default;

 protected:
  // Implements brillo::DBusServiceDaemon.
  void RegisterDBusObjectsAsync(
      brillo::dbus_utils::AsyncEventSequencer* sequencer) override;
  // Callback from Daemon to notify that the message loop exits and before
  // Daemon::Run() returns.
  void OnShutdown(int* exit_code) override;

 private:
  // The file path of the patchpanel daemon binary.
  base::FilePath cmd_path_;

  // Unique instance of patchpanel::System shared for all subsystems.
  std::unique_ptr<System> system_;
  // The singleton instance that manages the creation and exit notification of
  // each subprocess. All the subprocesses should be created by this.
  net_base::ProcessManager* process_manager_;
  // UMA metrics client.
  std::unique_ptr<MetricsLibraryInterface> metrics_;

  // Patchpanel adaptor.
  std::unique_ptr<PatchpanelAdaptor> adaptor_;
};

}  // namespace patchpanel

#endif  // PATCHPANEL_PATCHPANEL_DAEMON_H_
