// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_VPN_IPSEC_CONNECTION_H_
#define SHILL_VPN_IPSEC_CONNECTION_H_

#include <memory>

#include <base/callback.h>
#include <base/files/file_path.h>
#include <base/files/scoped_temp_dir.h>

#include "shill/mockable.h"
#include "shill/service.h"
#include "shill/vpn/vpn_connection.h"
#include "shill/vpn/vpn_util.h"

namespace shill {

// IPsecConnection manages the IPsec connection by starting charon process and
// taking to it via swanctl.
// When connecting, IPsecConnection will do the following steps to initiate the
// IPsec connection:
// - Create a temp dir in `/run/ipsec/` to store all the configuration files;
// - Generate strongswan.conf in the created temp dir;
// - Start charon process which is the IKE daemon;
// - Generate swanctl.conf in the created temp dir;
// - Invoke swanctl to let charon load the configurations in swanctl.conf;
// - Invoke swanctl to initiate the connection;
// TODO(b/165170125): Document temporary files.
class IPsecConnection : public VPNConnection {
 public:
  // TODO(b/165170125): Add fields.
  struct Config {};

  // This enum is only used internally. It need to be public to be accessible in
  // tests. Each value represents an step in the connect procedure. Also see
  // ScheduleConnectTask().
  enum class ConnectStep {
    kStart,
    kStrongSwanConfigWritten,
    kSwanctlConfigWritten,
    kCharonStarted,
    kSwanctlConfigLoaded,
    kIPsecConnected,
  };

  explicit IPsecConnection(std::unique_ptr<Config> config,
                           std::unique_ptr<Callbacks> callbacks,
                           EventDispatcher* dispatcher);
  ~IPsecConnection();

 private:
  friend class IPsecConnectionUnderTest;

  void OnConnect() override;
  void OnDisconnect() override;

  // Run tasks for connecting in order based on the current |step|.
  mockable void ScheduleConnectTask(ConnectStep step);

  // Tasks scheduled by ScheduleConnectTask(). Each function should call
  // ScheduleConnectTask() (either directly or using a callback) on the task
  // done, or call NoitfyFailure() to indicate a failure.

  // Generates strongswan.conf. On success, this function will trigger
  // |kStrongSwanConfigWritten| step and set |strongswan_conf_path_|.
  void WriteStrongSwanConfig();
  void StartCharon();
  void WriteSwanctlConfig();
  void SwanctlLoadConfig();
  void SwanctlInitiateConnection();

  std::unique_ptr<Config> config_;

  base::ScopedTempDir temp_dir_;
  base::FilePath strongswan_conf_path_;

  std::unique_ptr<VPNUtil> vpn_util_;

  base::WeakPtrFactory<IPsecConnection> weak_factory_{this};
};

}  // namespace shill

#endif  // SHILL_VPN_IPSEC_CONNECTION_H_
