// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IIOSERVICE_IIOSERVICE_SIMPLECLIENT_DAEMON_QUERY_H_
#define IIOSERVICE_IIOSERVICE_SIMPLECLIENT_DAEMON_QUERY_H_

#include <memory>
#include <string>
#include <vector>

#include <base/memory/weak_ptr.h>
#include <brillo/daemons/dbus_daemon.h>
#include <mojo/core/embedder/scoped_ipc_support.h>

#include "iioservice/iioservice_simpleclient/query_impl.h"
#include "iioservice/libiioservice_ipc/sensor_client_dbus.h"

namespace iioservice {

class DaemonQuery : public brillo::DBusDaemon, public SensorClientDbus {
 public:
  DaemonQuery(cros::mojom::DeviceType device_type,
              std::vector<std::string> attributes);
  ~DaemonQuery() override;

 protected:
  // brillo::DBusDaemon overrides:
  int OnInit() override;

 private:
  // SensorClientDbus overrides:
  void OnClientReceived(
      mojo::PendingReceiver<cros::mojom::SensorHalClient> client) override;

  // Responds to Mojo disconnection by quitting the daemon.
  void OnMojoDisconnect();

  cros::mojom::DeviceType device_type_;
  std::vector<std::string> attributes_;

  QueryImpl::ScopedQueryImpl query_ = {nullptr,
                                       SensorClient::SensorClientDeleter};

  // IPC Support
  std::unique_ptr<mojo::core::ScopedIPCSupport> ipc_support_;

  // Must be last class member.
  base::WeakPtrFactory<DaemonQuery> weak_ptr_factory_;
};

}  // namespace iioservice

#endif  // IIOSERVICE_IIOSERVICE_SIMPLECLIENT_DAEMON_QUERY_H_
